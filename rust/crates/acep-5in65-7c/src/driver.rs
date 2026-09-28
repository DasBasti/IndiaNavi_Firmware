//! Panel driver and its DisplayTarget impl (4bpp packed framebuffer, run-length decompression).
//!
//! Replaces: lib/Platinenmacher_HAL_ESP32/display/eink/acep_5in65_7c.c
//!
//! `C:<n>` comments throughout refer to line `n` of that file. The C driver
//! kept its state in file-scope statics (`fb`, `spi`, `dev`); here it lives in
//! [`Acep5In65`], which owns the framebuffer and the four HAL handles.

use alloc::boxed::Box;
use alloc::vec;

use embedded_hal::delay::DelayNs;
use embedded_hal::digital::{InputPin, OutputPin};
use embedded_hal::spi::SpiDevice;

use crate::command;

/// Panel width in pixels. C:16 (`ACEP_5IN65_WIDTH`).
pub const WIDTH: u16 = 600;
/// Panel height in pixels. C:17 (`ACEP_5IN65_HEIGHT`).
pub const HEIGHT: u16 = 448;

/// Framebuffer size in bytes: two 4-bit pixels per byte. C:15 (`FB_SIZE`).
pub const FB_SIZE: usize = (WIDTH as usize * HEIGHT as usize) / 2;

/// Number of pixels the framebuffer holds, i.e. the exclusive upper bound the
/// C driver compares against at C:41 (`FB_SIZE * 2`).
const FB_PIXELS: i32 = (FB_SIZE * 2) as i32;

/// How long the BUSY poll sleeps between samples. C:137, C:150
/// (`vTaskDelay(pdMS_TO_TICKS(1000))`).
pub const BUSY_POLL_MS: u32 = 1000;

/// Poll count at which the BUSY wait gives up, so the wait is bounded at
/// roughly 61 s rather than hanging forever. C:138, C:153 (`timeout++ == 60`;
/// the post-increment means the 61st poll is the one that fails).
pub const BUSY_TIMEOUT_POLLS: u8 = 60;

/// Delay the display power rail is held off for during a power cycle.
/// src/esp32/gui.c:364 `vTaskDelay(300)` at `CONFIG_FREERTOS_HZ=100`.
pub const POWER_OFF_MS: u32 = 3000;

/// Settle time after the display power rail comes back up.
/// src/esp32/gui.c:366 `vTaskDelay(10)` at `CONFIG_FREERTOS_HZ=100`.
pub const POWER_ON_SETTLE_MS: u32 = 100;

/// Screen rotation. Ports `display_rotation_t`
/// (lib/Platinenmacher/display.h:19-25); discriminants are pinned to the C
/// enum's.
///
/// Local to this crate rather than taken from `pm_core` so that the driver
/// builds without it; see [`crate::display_target`].
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum Rotation {
    /// No rotation.
    #[default]
    Deg0 = 0,
    /// 90 degrees clockwise.
    Deg90 = 1,
    /// 180 degrees clockwise.
    Deg180 = 2,
    /// 270 degrees clockwise.
    Deg270 = 3,
}

/// Anything that can go wrong talking to the panel.
///
/// SPI and GPIO errors are flattened to `embedded-hal`'s `ErrorKind` so that
/// this type stays free of the four HAL type parameters. Ports the subset of
/// `error_code_t` (lib/Platinenmacher/error.h) the C driver actually returned:
/// `TIMEOUT` (C:140, C:155) and `OUT_OF_BOUNDS` (C:54).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    /// The SPI device reported a failure. The C driver instead `assert`ed on
    /// `ESP_OK` (C:104, C:121) and rebooted the board.
    Spi(embedded_hal::spi::ErrorKind),
    /// A GPIO reported a failure. No C counterpart: the ESP-IDF GPIO calls the
    /// C driver used returned void.
    Pin(embedded_hal::digital::ErrorKind),
    /// BUSY did not reach the expected level within [`BUSY_TIMEOUT_POLLS`]
    /// polls. C:140, C:155.
    Timeout,
    /// The pixel is outside the framebuffer. C:54.
    OutOfBounds,
}

/// Result alias for this crate.
pub type Result<T> = core::result::Result<T, Error>;

/// Waveshare ACeP 5.65" 600x448 7-colour e-paper panel.
///
/// `PWR` drives the active-low display regulator enable (`EINK_VCC_nEN`,
/// include/pins.h:21). The C tree wired that pin up in the GUI task
/// (src/esp32/gui.c:358-368) rather than in the driver; it lives here now so
/// that the panel's power sequencing is in one place. `DC` is the command/data
/// select the C driver toggled from its SPI pre-transfer callback (C:162-166).
pub struct Acep5In65<SPI, DC, PWR, BUSY, DELAY> {
    spi: SPI,
    dc: DC,
    power: PWR,
    busy: BUSY,
    delay: DELAY,
    rotation: Rotation,
    /// Packed 4bpp framebuffer. C:16 declared this as a file-scope
    /// `uint8_t fb[FB_SIZE]`; it is heap allocated here because 134400 bytes
    /// does not belong on an embedded stack.
    fb: Box<[u8]>,
}

impl<SPI, DC, PWR, BUSY, DELAY> Acep5In65<SPI, DC, PWR, BUSY, DELAY>
where
    SPI: SpiDevice<u8>,
    DC: OutputPin,
    PWR: OutputPin,
    BUSY: InputPin,
    DELAY: DelayNs,
{
    /// Wrap already-configured peripherals. The SPI device must be mode 0 at
    /// 1 MHz with no MISO, matching the `spi_device_interface_config_t` the C
    /// driver built at C:183-189.
    ///
    /// Does not touch the hardware; call [`Self::init`] for that.
    pub fn new(spi: SPI, dc: DC, power: PWR, busy: BUSY, delay: DELAY, rotation: Rotation) -> Self {
        Self {
            spi,
            dc,
            power,
            busy,
            delay,
            rotation,
            // C:16 zero-initialises the framebuffer.
            fb: vec![0u8; FB_SIZE].into_boxed_slice(),
        }
    }

    /// Give the peripherals back.
    pub fn release(self) -> (SPI, DC, PWR, BUSY, DELAY) {
        (self.spi, self.dc, self.power, self.busy, self.delay)
    }

    /// Logical size in pixels, which swaps under a quarter turn. C:201-210,
    /// where the C driver chose the `display_init` arguments the same way.
    pub fn size(&self) -> (u16, u16) {
        match self.rotation {
            Rotation::Deg90 | Rotation::Deg270 => (HEIGHT, WIDTH),
            _ => (WIDTH, HEIGHT),
        }
    }

    /// Configured rotation.
    pub fn rotation(&self) -> Rotation {
        self.rotation
    }

    /// The packed framebuffer, for tests and for callers that want to blit
    /// into it directly. C exposed this as the global `fb`.
    pub fn framebuffer(&self) -> &[u8] {
        &self.fb
    }

    /// Bring the display regulator up. `EINK_VCC_nEN` is active low
    /// (src/esp32/gui.c:359 sets `onValue = GPIO_RESET`).
    pub fn power_on(&mut self) -> Result<()> {
        self.power.set_low().map_err(pin_err)
    }

    /// Cut the display regulator.
    pub fn power_off(&mut self) -> Result<()> {
        self.power.set_high().map_err(pin_err)
    }

    /// Off, wait, on, settle -- the sequence the GUI task ran before every
    /// [`Self::init`] attempt. src/esp32/gui.c:362-367.
    pub fn power_cycle(&mut self) -> Result<()> {
        self.power_off()?;
        self.delay.delay_ms(POWER_OFF_MS);
        self.power_on()?;
        self.delay.delay_ms(POWER_ON_SETTLE_MS);
        Ok(())
    }

    /// Toggle the panel reset pin.
    ///
    /// C:85-88 `ACEP_5IN65_Reset` is an empty function that returns
    /// immediately -- this panel is reset by cycling its regulator, which is
    /// what [`Self::power_cycle`] does. Kept so the init sequence still has
    /// the step the C driver called at C:226, and so the deviation is
    /// explicit rather than a silently dropped line.
    pub fn reset(&mut self) -> Result<()> {
        Ok(())
    }

    /// Run the panel init sequence. C:171-276.
    ///
    /// Returns [`Error::Timeout`] if BUSY never goes high, which is the
    /// `display_busyhigh_timeout` path at C:227-228 where the C driver
    /// returned `NULL` and the GUI task power cycled and retried.
    pub fn init(&mut self) -> Result<()> {
        // C:221 -- D/C parked low before the first transfer. CS is owned by
        // the SpiDevice, so C:222's `gpio_set_level(dev->select, 0)` has no
        // counterpart.
        self.dc.set_low().map_err(pin_err)?;

        self.reset()?; // C:226
        self.wait_busy_high()?; // C:227-228

        for entry in command::INIT_SEQUENCE {
            // C:229-259
            self.send_command(entry.cmd)?;
            self.send_data(entry.data)?;
        }

        self.delay.delay_ms(command::INIT_DELAY_MS); // C:261

        for entry in command::INIT_SEQUENCE_AFTER_DELAY {
            // C:262-263
            self.send_command(entry.cmd)?;
            self.send_data(entry.data)?;
        }

        Ok(())
    }

    /// Write one pixel into the framebuffer. C:26-55 (`ACEP_5IN65_Write`).
    ///
    /// `color` is a 7-colour panel code; only its low nibble is used (C:46,
    /// C:50). Returns [`Error::OutOfBounds`] for any coordinate that lands
    /// outside the framebuffer, exactly where C:41 did.
    pub fn write_pixel(&mut self, x: i16, y: i16, color: u8) -> Result<()> {
        let x = i32::from(x);
        let y = i32::from(y);
        let w = i32::from(WIDTH);
        let h = i32::from(HEIGHT);

        let position = match self.rotation {
            // C:32-34 -- switch x and y and invert.
            Rotation::Deg270 => ((h - (x + 1)) * w) + y,
            // C:35-37 -- switch x and y and mirror both axes.
            Rotation::Deg90 => (x * w) + w - y - 1,
            // C:38-39 -- rotate 0 (and 180) leave the mapping alone.
            _ => (y * w) + x,
        };

        // C:41. The C code held `position` in a uint32_t, so a negative value
        // wrapped to something far above FB_SIZE*2 and fell out here; the
        // explicit lower bound is the same test written honestly.
        if !(0..FB_PIXELS).contains(&position) {
            return Err(Error::OutOfBounds); // C:54
        }
        let position = position as usize;
        let byte = &mut self.fb[position >> 1];

        if position & 0x1 == 1 {
            // C:46 -- odd pixels occupy the low nibble.
            *byte = (*byte & 0xf0) | (color & 0x0f);
        } else {
            // C:50 -- even pixels occupy the high nibble.
            *byte = (*byte & 0x0f) | ((color & 0x0f) << 4);
        }
        Ok(()) // C:52
    }

    /// Push the framebuffer to the panel and refresh it. C:60-64 wrapping
    /// C:281-308 (`ACEP_5IN65_Commit_Fb` / `ACEP_5IN65_Display`).
    ///
    /// The C wrapper swallowed a timeout into an `ESP_LOGE` (C:62-63); here it
    /// is returned. A full refresh takes the panel tens of seconds, so both
    /// BUSY waits really can run for a while.
    pub fn update(&mut self) -> Result<()> {
        // C:284-288 -- resolution is re-sent before every frame.
        self.send_command(command::RESOLUTION_SETTING)?;
        self.send_data(&command::RESOLUTION_DATA)?;

        self.send_command(command::DATA_START_TRANSMISSION_1)?; // C:289

        // C:290-296 sends the framebuffer one byte per SPI transaction,
        // row by row; since the rows are contiguous that is the same MOSI
        // byte stream as one transfer of the whole buffer, which is what we
        // do. `self.dc` and `self.fb` are disjoint fields, hence the inlined
        // send_data.
        self.dc.set_high().map_err(pin_err)?;
        self.spi.write(&self.fb).map_err(spi_err)?;

        self.send_command(command::POWER_ON)?; // C:297
        self.wait_busy_high()?; // C:298-299
        self.send_command(command::DISPLAY_REFRESH)?; // C:300
        self.wait_busy_high()?; // C:301-302
        self.send_command(command::POWER_OFF)?; // C:303
        self.wait_busy_low()?; // C:305-306

        Ok(()) // C:307
    }

    /// Put the panel into deep sleep. C:355-359 (`ACEP_5IN65_Sleep`).
    pub fn sleep(&mut self) -> Result<()> {
        self.send_command(command::DEEP_SLEEP)?; // C:357
        self.send_data(&[command::DEEP_SLEEP_CHECK_CODE]) // C:358
    }

    /// Send one opcode with D/C low. C:95-105 (`ACEP_5IN65_SendCommand`,
    /// whose `t.user = 0` made the pre-transfer callback at C:162-166 pull D/C
    /// low).
    fn send_command(&mut self, cmd: u8) -> Result<()> {
        self.dc.set_low().map_err(pin_err)?;
        self.spi.write(&[cmd]).map_err(spi_err)
    }

    /// Send payload bytes with D/C high. C:112-122 (`ACEP_5IN65_SendData`,
    /// `t.user = 1`).
    fn send_data(&mut self, data: &[u8]) -> Result<()> {
        self.dc.set_high().map_err(pin_err)?;
        self.spi.write(data).map_err(spi_err)
    }

    /// Block until BUSY is high. C:132-144 (`ACEP_5IN65_BusyHigh`).
    fn wait_busy_high(&mut self) -> Result<()> {
        self.wait_busy(true)
    }

    /// Block until BUSY is low. C:146-158 (`ACEP_5IN65_BusyLow`).
    fn wait_busy_low(&mut self) -> Result<()> {
        self.wait_busy(false)
    }

    /// Shared body of C:132-158. Both C helpers sample the pin, sleep a
    /// second, then bump a counter that trips on the 61st pass; that ordering
    /// is preserved so the bound is the same ~61 s.
    fn wait_busy(&mut self, want_high: bool) -> Result<()> {
        let mut polls: u8 = 0;
        loop {
            let level = self.busy.is_high().map_err(pin_err)?; // C:135, C:149
            if level == want_high {
                return Ok(()); // C:143, C:157
            }
            self.delay.delay_ms(BUSY_POLL_MS); // C:137, C:151
            if polls == BUSY_TIMEOUT_POLLS {
                return Err(Error::Timeout); // C:140, C:155
            }
            polls += 1; // the `timeout++` of C:138, C:153
        }
    }
}

/// Colour of the pixel at `x`/`y` in a packed 4bpp image `data` that is
/// `width` pixels wide. C:69-79 (`ACEP_5IN65_Decompress_Pixel`).
///
/// This is the hook `display_draw_image` (lib/Platinenmacher/display.c:372-387)
/// calls once per pixel to unpack a blitted image; the C driver installed it
/// as `disp->decompress` at C:214.
///
/// Note the mask: C:76 and C:78 keep three bits, not four, so an image byte
/// can never produce colour 8-15. Out of range reads return 0x07, the
/// `TRANSPARENT` code (lib/Platinenmacher/colors.h:23), which
/// `display_pixel_draw` skips; the C version indexed `data` unchecked and read
/// past the end.
pub fn decompress_pixel(width: u16, x: i16, y: i16, data: &[u8]) -> u8 {
    let pos = i32::from(y) * i32::from(width) + i32::from(x); // C:72
    if pos < 0 {
        return 0x07;
    }
    let pos = pos as usize;
    let byte = match data.get(pos >> 1) {
        Some(b) => *b,
        None => return 0x07,
    };
    if pos & 0x1 == 1 {
        byte & 0x7 // C:76
    } else {
        (byte >> 4) & 0x7 // C:78
    }
}

fn spi_err<E: embedded_hal::spi::Error>(e: E) -> Error {
    Error::Spi(e.kind())
}

fn pin_err<E: embedded_hal::digital::Error>(e: E) -> Error {
    Error::Pin(e.kind())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mock::{driver, driver_with};
    use alloc::vec::Vec;

    /// C:229-263, transcribed by hand from the C file rather than from
    /// [`crate::command::INIT_SEQUENCE`], so that the test actually pins the
    /// bytes instead of agreeing with the table it is checking.
    /// `false` = D/C low (command byte), `true` = D/C high (data byte).
    #[rustfmt::skip]
    const EXPECTED_INIT: &[(bool, u8)] = &[
        (false, 0x00), (true, 0xEF), (true, 0x08),                               // C:229-231
        (false, 0x01), (true, 0x37), (true, 0x00), (true, 0x23), (true, 0x23),   // C:232-236
        (false, 0x03), (true, 0x00),                                             // C:237-238
        (false, 0x06), (true, 0xC7), (true, 0xC7), (true, 0x1D),                 // C:239-242
        (false, 0x30), (true, 0x3C),                                             // C:243-244
        (false, 0x41), (true, 0x80),                                             // C:245-246
        (false, 0x50), (true, 0x3F),                                             // C:247-248
        (false, 0x60), (true, 0x22),                                             // C:249-250
        (false, 0x61), (true, 0x02), (true, 0x58), (true, 0x01), (true, 0xC0),   // C:251-255
        (false, 0xE3), (true, 0xAA),                                             // C:256-257
        (false, 0x82), (true, 0x80),                                             // C:258-259
        // C:261 vTaskDelay(10) sits here.
        (false, 0x50), (true, 0x37),                                             // C:262-263
    ];

    #[test]
    fn geometry_matches_the_c_defines() {
        assert_eq!(WIDTH, 600); // C:16
        assert_eq!(HEIGHT, 448); // C:17
                                 // C:15 FB_SIZE = WIDTH * HEIGHT / 2, i.e. two 4-bit pixels per byte.
        assert_eq!(FB_SIZE, 600 * 448 / 2);
        assert_eq!(FB_SIZE, 134_400);
    }

    #[test]
    fn logical_size_swaps_on_a_quarter_turn() {
        // C:201-210.
        assert_eq!(driver(Rotation::Deg0).0.size(), (600, 448));
        assert_eq!(driver(Rotation::Deg180).0.size(), (600, 448));
        assert_eq!(driver(Rotation::Deg90).0.size(), (448, 600));
        assert_eq!(driver(Rotation::Deg270).0.size(), (448, 600));
    }

    #[test]
    fn init_emits_the_c_byte_sequence() {
        let (mut dsp, rec) = driver(Rotation::Deg90);
        dsp.init().unwrap();

        let rec = rec.borrow();
        assert_eq!(rec.flat(), EXPECTED_INIT.to_vec());
        assert_eq!(rec.byte_count(), 33);
        // C:261 -- ten 10 ms ticks, and nothing else waits during init
        // because BUSY was already high at C:227.
        assert_eq!(rec.delays_ms, [command::INIT_DELAY_MS]);
        // The framebuffer is untouched by init.
        assert!(dsp.framebuffer().iter().all(|b| *b == 0));
    }

    #[test]
    fn init_waits_for_busy_before_talking_to_the_panel() {
        // BUSY low for two polls, then high. C:227 -> C:132-144.
        let (mut dsp, rec) = driver_with(Rotation::Deg0, &[false, false], true);
        dsp.init().unwrap();

        let rec = rec.borrow();
        // Two BUSY polls of 1000 ms, then the 100 ms mid-sequence pause.
        assert_eq!(
            rec.delays_ms,
            [BUSY_POLL_MS, BUSY_POLL_MS, command::INIT_DELAY_MS]
        );
        assert_eq!(rec.flat(), EXPECTED_INIT.to_vec());
    }

    #[test]
    fn init_times_out_instead_of_hanging_when_busy_never_rises() {
        // C:227-228 `goto display_busyhigh_timeout`.
        let (mut dsp, rec) = driver_with(Rotation::Deg0, &[], false);
        assert_eq!(dsp.init(), Err(Error::Timeout));

        let rec = rec.borrow();
        // C:138 `timeout++ == 60` trips on the 61st poll, so the wait is
        // bounded at 61 s -- it does not spin forever.
        assert_eq!(rec.delays_ms.len(), usize::from(BUSY_TIMEOUT_POLLS) + 1);
        assert!(rec.delays_ms.iter().all(|ms| *ms == BUSY_POLL_MS));
        // Nothing was sent to the panel.
        assert_eq!(rec.byte_count(), 0);
    }

    #[test]
    fn full_flush_emits_the_expected_byte_count() {
        let (mut dsp, rec) = driver_with(Rotation::Deg0, &[true, true, false], false);
        dsp.update().unwrap();

        let rec = rec.borrow();
        // C:284-303: 0x61 + 4 resolution bytes + 0x10 + one byte per two
        // pixels + 0x04 + 0x12 + 0x02.
        assert_eq!(rec.byte_count(), 1 + 4 + 1 + FB_SIZE + 1 + 1 + 1);
        assert_eq!(rec.byte_count(), 134_409);

        let flat = rec.flat();
        // Header, C:284-289.
        assert_eq!(
            flat[..6],
            [
                (false, 0x61),
                (true, 0x02),
                (true, 0x58),
                (true, 0x01),
                (true, 0xC0),
                (false, 0x10)
            ]
        );
        // The frame itself goes out as data, C:290-296.
        assert!(flat[6..6 + FB_SIZE].iter().all(|(is_data, _)| *is_data));
        // Trailer, C:297-303.
        assert_eq!(
            flat[6 + FB_SIZE..],
            [(false, 0x04), (false, 0x12), (false, 0x02)]
        );
    }

    #[test]
    fn flush_sends_the_framebuffer_verbatim() {
        let (mut dsp, rec) = driver_with(Rotation::Deg0, &[true, true, false], false);
        // Two pixels sharing byte 0, and one at the far end of the buffer.
        dsp.write_pixel(0, 0, 0x04).unwrap();
        dsp.write_pixel(1, 0, 0x06).unwrap();
        dsp.write_pixel(599, 447, 0x02).unwrap();
        let expected: Vec<u8> = dsp.framebuffer().to_vec();
        assert_eq!(expected[0], 0x46);
        assert_eq!(expected[FB_SIZE - 1], 0x02);

        dsp.update().unwrap();

        let rec = rec.borrow();
        let sent: Vec<u8> = rec
            .flat()
            .into_iter()
            .skip(6)
            .take(FB_SIZE)
            .map(|(_, b)| b)
            .collect();
        assert_eq!(sent, expected);
    }

    #[test]
    fn flush_times_out_if_the_refresh_never_finishes() {
        // BUSY stuck low after power-on: C:298-299 returns TIMEOUT.
        let (mut dsp, _rec) = driver_with(Rotation::Deg0, &[], false);
        assert_eq!(dsp.update(), Err(Error::Timeout));

        // BUSY stuck high after power-off: C:305-306 returns TIMEOUT.
        let (mut dsp, _rec) = driver_with(Rotation::Deg0, &[], true);
        assert_eq!(dsp.update(), Err(Error::Timeout));
    }

    #[test]
    fn pixels_pack_two_to_a_byte_high_nibble_first() {
        let (mut dsp, _rec) = driver(Rotation::Deg0);
        // C:50 -- even pixel index goes in the high nibble.
        dsp.write_pixel(0, 0, 0x05).unwrap();
        assert_eq!(dsp.framebuffer()[0], 0x50);
        // C:46 -- odd pixel index goes in the low nibble, leaving the other
        // pixel alone.
        dsp.write_pixel(1, 0, 0x03).unwrap();
        assert_eq!(dsp.framebuffer()[0], 0x53);
        // Overwriting one nibble must not disturb its neighbour.
        dsp.write_pixel(0, 0, 0x01).unwrap();
        assert_eq!(dsp.framebuffer()[0], 0x13);
        // Only the low nibble of the colour is used, C:46/C:50.
        dsp.write_pixel(1, 0, 0xF7).unwrap();
        assert_eq!(dsp.framebuffer()[0], 0x17);
    }

    #[test]
    fn rotation_places_pixels_where_the_c_driver_did() {
        // C:38-39, position = y * WIDTH + x.
        let (mut dsp, _rec) = driver(Rotation::Deg0);
        dsp.write_pixel(2, 1, 0x06).unwrap();
        assert_eq!(dsp.framebuffer()[(600 + 2) / 2], 0x60);

        // C:35-37, position = x * WIDTH + WIDTH - y - 1. x=0,y=0 -> 599.
        let (mut dsp, _rec) = driver(Rotation::Deg90);
        dsp.write_pixel(0, 0, 0x06).unwrap();
        assert_eq!(dsp.framebuffer()[599 / 2], 0x06);

        // C:32-34, position = (HEIGHT - (x + 1)) * WIDTH + y. x=0,y=0 ->
        // 447 * 600 = 268200.
        let (mut dsp, _rec) = driver(Rotation::Deg270);
        dsp.write_pixel(0, 0, 0x06).unwrap();
        assert_eq!(dsp.framebuffer()[268_200 / 2], 0x60);

        // C:38 -- 180 falls through to the unrotated mapping, as in C.
        let (mut dsp, _rec) = driver(Rotation::Deg180);
        dsp.write_pixel(2, 1, 0x06).unwrap();
        assert_eq!(dsp.framebuffer()[(600 + 2) / 2], 0x60);
    }

    #[test]
    fn out_of_bounds_pixels_are_rejected() {
        // C:41/C:54 -- anything that does not land inside FB_SIZE*2 pixels.
        let (mut dsp, _rec) = driver(Rotation::Deg0);
        assert_eq!(dsp.write_pixel(-1, 0, 1), Err(Error::OutOfBounds));
        assert_eq!(dsp.write_pixel(0, -1, 1), Err(Error::OutOfBounds));
        // y = 447 is the last row, so x = 600 is one past the buffer.
        assert_eq!(dsp.write_pixel(600, 447, 1), Err(Error::OutOfBounds));
        assert_eq!(dsp.write_pixel(0, 448, 1), Err(Error::OutOfBounds));
        assert_eq!(dsp.write_pixel(599, 447, 1), Ok(()));
        assert!(dsp.framebuffer().iter().filter(|b| **b != 0).count() == 1);

        let (mut dsp, _rec) = driver(Rotation::Deg270);
        assert_eq!(dsp.write_pixel(448, 0, 1), Err(Error::OutOfBounds));
        assert_eq!(dsp.write_pixel(447, 599, 1), Ok(()));
    }

    #[test]
    fn decompress_unpacks_three_bit_colours() {
        // C:69-79. Even pixel -> high nibble, odd pixel -> low nibble, three
        // bits kept either way.
        let data = [0x12u8, 0x34, 0x56];
        assert_eq!(decompress_pixel(2, 0, 0, &data), 0x1);
        assert_eq!(decompress_pixel(2, 1, 0, &data), 0x2);
        assert_eq!(decompress_pixel(2, 0, 1, &data), 0x3);
        assert_eq!(decompress_pixel(2, 1, 1, &data), 0x4);
        assert_eq!(decompress_pixel(2, 0, 2, &data), 0x5);
        assert_eq!(decompress_pixel(2, 1, 2, &data), 0x6);
        // C:76/C:78 mask with 0x7, so bit 3 is dropped, not returned.
        assert_eq!(decompress_pixel(1, 0, 0, &[0xF8]), 0x7);
        assert_eq!(decompress_pixel(1, 0, 0, &[0x8F]), 0x0);
        // Past the end of the image the C code read out of bounds; we report
        // TRANSPARENT, which display_pixel_draw skips.
        assert_eq!(decompress_pixel(2, 0, 9, &data), 0x7);
        assert_eq!(decompress_pixel(2, -1, 0, &data), 0x7);
    }

    #[test]
    fn sleep_sends_the_deep_sleep_check_code() {
        // C:355-359.
        let (mut dsp, rec) = driver(Rotation::Deg0);
        dsp.sleep().unwrap();
        assert_eq!(rec.borrow().flat(), [(false, 0x07), (true, 0xA5)]);
    }

    #[test]
    fn power_cycle_matches_the_gui_task_sequence() {
        // src/esp32/gui.c:362-367. EINK_VCC_nEN is active low, so "off" is a
        // high level and "on" is a low one.
        let (mut dsp, rec) = driver(Rotation::Deg0);
        dsp.power_cycle().unwrap();

        let rec = rec.borrow();
        assert_eq!(rec.power, [true, false]);
        assert_eq!(rec.delays_ms, [POWER_OFF_MS, POWER_ON_SETTLE_MS]);
        assert_eq!(rec.byte_count(), 0);
    }

    #[test]
    fn reset_is_the_no_op_the_c_driver_had() {
        // C:85-88 returns immediately without touching a pin.
        let (mut dsp, rec) = driver(Rotation::Deg0);
        dsp.reset().unwrap();
        let rec = rec.borrow();
        assert!(rec.power.is_empty());
        assert_eq!(rec.byte_count(), 0);
        assert!(rec.delays_ms.is_empty());
    }
}
