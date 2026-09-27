/*
 * Parser for PMTK commands and responses
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

//! PMTK, the MediaTek command set the L96 speaks.
//!
//! Ported from `lib/nmea_parser/pmtk_parser.c`. Where the C code logs a result
//! with `ESP_LOGI`, the port records it on [`PmtkPlugin`] so that callers, and
//! tests, can read it back.

use core::fmt::Write;

use crate::buf::FixedStr;
use crate::checksum::checksum;
use crate::error::{Error, Result};
use crate::plugin::{atoi, SentenceItem, SentencePlugin};

/// `PMTK_ACK`: the acknowledgement of a command, `$PMTK001,...`.
pub const PMTK_ACK: u32 = 1;
/// `PMTK_SYS_MSG`: a system message, `$PMTK010,...`.
pub const PMTK_SYS_MSG: u32 = 10;
/// `PMTK_TXT_MSG`: a free text message, `$PMTK011,...`.
pub const PMTK_TXT_MSG: u32 = 11;
/// `PMTK_CMD_STANDBY_MODE`: enter standby, `$PMTK161,...`.
pub const PMTK_CMD_STANDBY_MODE: u32 = 161;
/// `PMTK_CMD_PERIODIC_MODE`: select a power mode, `$PMTK225,...`.
pub const PMTK_CMD_PERIODIC_MODE: u32 = 225;
/// `PMTK_CMD_AIC_MODE`: active interference cancellation, `$PMTK286,...`.
pub const PMTK_CMD_AIC_MODE: u32 = 286;
/// `PMTK_API_SET_GNSS_SEARCH_MODE`: pick the constellations, `$PMTK353,...`.
pub const PMTK_API_SET_GNSS_SEARCH_MODE: u32 = 353;

/// Longest PMTK command [`PmtkCommand`] can build, `$` and `\r\n` included.
pub const PMTK_COMMAND_MAX: usize = 96;
/// Longest text a `PMTK_TXT_MSG` can carry into [`PmtkPlugin`].
pub const PMTK_TEXT_MAX: usize = 64;

/// The system messages of `PMTK_SYS_MSG`, indexed by its argument.
///
/// The `pmtkSystemMessages` table of `pmtk_parser.c`, typos included.
pub const PMTK_SYSTEM_MESSAGES: [&str; 4] = [
    "Unknown",
    "Startup",
    "Notification for host aiding EPO",
    "Notification for the transisiton to normal mode done successfully",
];

/// The system message an argument of `PMTK_SYS_MSG` denotes, or `None` when it
/// is outside [`PMTK_SYSTEM_MESSAGES`].
///
/// The C code indexes the table unchecked, so an unexpected argument reads past
/// its end.
pub fn system_message(index: i32) -> Option<&'static str> {
    PMTK_SYSTEM_MESSAGES
        .get(usize::try_from(index).ok()?)
        .copied()
}

/// What the receiver made of an acknowledged command: field 2 of `$PMTK001`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PmtkAckFlag {
    /// 0: invalid packet type.
    InvalidPacket,
    /// 1: unsupported packet type.
    UnsupportedPacket,
    /// 2: valid packet, action failed.
    ActionFailed,
    /// 3: valid packet, action succeeded.
    ActionSucceeded,
}

impl PmtkAckFlag {
    /// The flag a field value denotes, or `None` for a value the C `switch`
    /// does not cover.
    pub const fn from_field(value: i32) -> Option<Self> {
        match value {
            0 => Some(PmtkAckFlag::InvalidPacket),
            1 => Some(PmtkAckFlag::UnsupportedPacket),
            2 => Some(PmtkAckFlag::ActionFailed),
            3 => Some(PmtkAckFlag::ActionSucceeded),
            _ => None,
        }
    }

    /// The `esp_err_t` the C code returns for this flag.
    pub const fn as_result(self) -> Result<()> {
        match self {
            PmtkAckFlag::InvalidPacket => Err(Error::InvalidArg),
            PmtkAckFlag::UnsupportedPacket => Err(Error::NotSupported),
            PmtkAckFlag::ActionFailed => Err(Error::Failed),
            PmtkAckFlag::ActionSucceeded => Ok(()),
        }
    }
}

/// An acknowledgement: which command, and how it went.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PmtkAck {
    /// The acknowledged packet type, field 1 of `$PMTK001`.
    pub packet_type: u32,
    /// The outcome, once field 2 has been seen.
    pub flag: Option<PmtkAckFlag>,
}

/// The constellations `PMTK_API_SET_GNSS_SEARCH_MODE` reports as enabled.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct GnssSearchMode {
    /// Field 3: GPS.
    pub gps: bool,
    /// Field 4: GLONASS.
    pub glonass: bool,
    /// Field 5: Galileo.
    pub galileo: bool,
    /// Field 6: Galileo full.
    pub galileo_full: bool,
    /// Field 7: BeiDou.
    pub beidou: bool,
}

/// A ready to send PMTK command, checksum and `\r\n` included.
///
/// The C code has no builder: `l96.h` spells the commands out as string
/// literals. [`crate::l96`] keeps those literals, and every one of them is what
/// this builder produces.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct PmtkCommand {
    text: FixedStr<PMTK_COMMAND_MAX>,
}

impl PmtkCommand {
    /// Builds `$PMTK<packet_type>,<args..>*<checksum>\r\n`.
    ///
    /// The packet type is padded to three digits, as the L96 documentation
    /// writes it. Fails with [`Error::InvalidArg`] when the command would be
    /// longer than [`PMTK_COMMAND_MAX`].
    ///
    /// ```
    /// use nmea::{l96, plugin::pmtk::PmtkCommand};
    ///
    /// let cmd = PmtkCommand::new(161, &["0"]).unwrap();
    /// assert_eq!(cmd.as_str(), l96::ENTER_STANDBY);
    /// ```
    pub fn new(packet_type: u32, args: &[&str]) -> Result<Self> {
        let mut payload = FixedStr::<PMTK_COMMAND_MAX>::new();
        write!(payload, "PMTK{packet_type:03}").map_err(|_| Error::InvalidArg)?;
        for arg in args {
            payload.push_str(",")?;
            payload.push_str(arg)?;
        }

        let crc = checksum(payload.as_bytes());
        const HEX: &[u8; 16] = b"0123456789ABCDEF";
        let digits = [HEX[(crc >> 4) as usize], HEX[(crc & 0x0f) as usize]];

        let mut text = FixedStr::new();
        text.push_str("$")?;
        text.push_str(payload.as_str())?;
        text.push_str("*")?;
        text.push_str(core::str::from_utf8(&digits).unwrap_or(""))?;
        text.push_str("\r\n")?;
        Ok(Self { text })
    }

    /// The command, ready for the UART.
    pub fn as_str(&self) -> &str {
        self.text.as_str()
    }

    /// The command bytes, ready for the UART.
    pub fn as_bytes(&self) -> &[u8] {
        self.text.as_bytes()
    }
}

/// Parses the PMTK sentences the L96 sends back.
///
/// Replaces `pmtk_detect()` and `pmtk_parse()`, and the `messageNumber` and
/// `message_id` statics they share.
#[derive(Debug, Default, Clone, Copy, PartialEq, Eq)]
pub struct PmtkPlugin {
    /// `messageNumber`: the PMTK number of the sentence being parsed.
    message_number: u32,
    /// `message_id`: the packet type an acknowledgement is about.
    message_id: u32,
    ack: Option<PmtkAck>,
    search_mode: GnssSearchMode,
    standby_mode: Option<i32>,
    system_message: Option<i32>,
    text_message: FixedStr<PMTK_TEXT_MAX>,
}

impl PmtkPlugin {
    /// A plugin that has seen nothing yet.
    pub const fn new() -> Self {
        Self {
            message_number: 0,
            message_id: 0,
            ack: None,
            search_mode: GnssSearchMode {
                gps: false,
                glonass: false,
                galileo: false,
                galileo_full: false,
                beidou: false,
            },
            standby_mode: None,
            system_message: None,
            text_message: FixedStr::new(),
        }
    }

    /// The PMTK number of the sentence being parsed, or 0 outside one.
    pub fn message_number(&self) -> u32 {
        self.message_number
    }

    /// The most recent acknowledgement.
    pub fn ack(&self) -> Option<PmtkAck> {
        self.ack
    }

    /// The constellations last reported by `PMTK_API_SET_GNSS_SEARCH_MODE`.
    pub fn search_mode(&self) -> GnssSearchMode {
        self.search_mode
    }

    /// The argument last echoed by `PMTK_CMD_STANDBY_MODE`.
    pub fn standby_mode(&self) -> Option<i32> {
        self.standby_mode
    }

    /// The argument of the most recent `PMTK_SYS_MSG`.
    pub fn system_message_id(&self) -> Option<i32> {
        self.system_message
    }

    /// The text of the most recent `PMTK_SYS_MSG`, when it is one this build
    /// knows; see [`system_message`].
    pub fn system_message(&self) -> Option<&'static str> {
        self.system_message.and_then(system_message)
    }

    /// The text of the most recent `PMTK_TXT_MSG`.
    pub fn text_message(&self) -> &str {
        self.text_message.as_str()
    }

    /// Whether an acknowledgement for `packet_type` carries fields this parser
    /// understands.
    ///
    /// Replaces the `message_parser` table of `pmtk_parser.c`.
    pub const fn handles_packet_type(packet_type: u32) -> bool {
        matches!(
            packet_type,
            PMTK_CMD_STANDBY_MODE | PMTK_API_SET_GNSS_SEARCH_MODE
        )
    }

    /// `parse_packet_type_161()`.
    fn parse_standby_mode(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        self.standby_mode = Some(item.as_int());
        Ok(())
    }

    /// `parse_packet_type_353()`.
    fn parse_gnss_search_mode(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        let enabled = item.first_byte() == b'1';
        match item.num {
            3 => self.search_mode.gps = enabled,
            4 => self.search_mode.glonass = enabled,
            5 => self.search_mode.galileo = enabled,
            6 => self.search_mode.galileo_full = enabled,
            7 => self.search_mode.beidou = enabled,
            _ => {}
        }
        Ok(())
    }

    /// The `message_parser` dispatch, by packet type rather than by slot.
    ///
    /// `pmtk_parser.c` indexes `message_parser` with the packet type itself
    /// (`message_parser[message_id]` for 161 or 353 over a two element array),
    /// which reads past the end of the table. Dispatching on the packet type is
    /// what the table is keyed by and what the lookup in its `case 1` branch
    /// already does.
    fn parse_packet_type(&mut self, packet_type: u32, item: &SentenceItem<'_>) -> Result<()> {
        match packet_type {
            PMTK_CMD_STANDBY_MODE => self.parse_standby_mode(item),
            PMTK_API_SET_GNSS_SEARCH_MODE => self.parse_gnss_search_mode(item),
            _ => Err(Error::NotSupported),
        }
    }

    /// `case 1`: `$PMTK001`, the acknowledgement of a command.
    fn parse_ack(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        match item.num {
            1 => {
                // Field 1 is the acknowledged packet type.
                self.message_id = item.as_int().max(0) as u32;
                self.ack = Some(PmtkAck {
                    packet_type: self.message_id,
                    flag: None,
                });
                if Self::handles_packet_type(self.message_id) {
                    Ok(())
                } else {
                    Err(Error::NotSupported)
                }
            }
            2 => {
                // Field 2 is the outcome.
                let flag = PmtkAckFlag::from_field(item.as_int());
                if let Some(ack) = self.ack.as_mut() {
                    ack.flag = flag;
                }
                match flag {
                    Some(flag) => flag.as_result(),
                    // The C `switch` has no default, so a flag outside 0..=3
                    // falls out of `pmtk_parse()` as `ESP_OK`.
                    None => Ok(()),
                }
            }
            _ => {
                if self.message_id != 0 {
                    let packet_type = self.message_id;
                    self.parse_packet_type(packet_type, item)
                } else {
                    Ok(())
                }
            }
        }
    }
}

impl SentencePlugin for PmtkPlugin {
    /// `pmtk_detect()`: claims every sentence whose identifier contains `PMTK`
    /// and reads the PMTK number that follows it.
    fn detect(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        match item.text.find("PMTK") {
            Some(offset) => {
                // `pmtk_detect()` reads from `item_str + 5`, which is the same
                // byte for the `"$PMTK..."` the receiver sends.
                let number = item.text.get(offset + "PMTK".len()..).unwrap_or("");
                self.message_number = atoi(number).max(0) as u32;
                Ok(())
            }
            None => Err(Error::NotSupported),
        }
    }

    /// `pmtk_parse()`.
    ///
    /// ```text
    /// $PMTK001,353,3,1,1,1,0,0,15*00
    /// 1: acknowledged packet type, 353 is PMTK_API_SET_GNSS_SEARCH_MODE
    /// 2: flag, 0 invalid packet, 1 unsupported, 2 action failed, 3 succeeded
    /// 3..: fields of the acknowledged command
    /// ```
    fn parse(&mut self, item: &SentenceItem<'_>) -> Result<()> {
        match self.message_number {
            PMTK_ACK => self.parse_ack(item),
            PMTK_SYS_MSG => {
                self.system_message = Some(item.as_int());
                Ok(())
            }
            PMTK_TXT_MSG => {
                // Overlong text is truncated rather than rejected; the C code
                // only logs it.
                let mut end = item.text.len().min(PMTK_TEXT_MAX);
                while end > 0 && !item.text.is_char_boundary(end) {
                    end -= 1;
                }
                let text = &item.text[..end];
                self.text_message.set(text)?;
                // `pmtk_parse()` clears `messageNumber` here, so any further
                // item of this sentence is ignored.
                self.message_number = 0;
                Ok(())
            }
            _ => Ok(()),
        }
    }
}
