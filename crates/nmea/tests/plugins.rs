/*
 * Tests for the PMTK and PQ plugins
 *
 * Copyright (c) 2023, Bastian Neumann <info@platinenmacher.tech>
 *
 * SPDX-License-Identifier: MIT
 */

use nmea::plugin::pmtk::{
    GnssSearchMode, PmtkAckFlag, PMTK_API_SET_GNSS_SEARCH_MODE, PMTK_CMD_STANDBY_MODE,
    PMTK_COMMAND_MAX,
};
use nmea::plugin::pq::{GlpAccess, PqStatement};
use nmea::{
    l96, verify, Dispatcher, Error, PluginRegistry, PmtkCommand, PmtkPlugin, PqPlugin,
    SentenceItem, SentencePlugin, Statement, GPS_MAX_PARSER_PLUGINS, STATEMENT_PLUGIN,
};

/// Feeds one whole sentence to a dispatcher over the plugins in the order
/// `src/esp32/gps.c` registers them, and hands the plugins back afterwards.
///
/// A fresh dispatcher per sentence is equivalent to one long lived dispatcher:
/// item 0 of a sentence resets the statement, and everything a plugin learns
/// lives in the plugin, not in the dispatcher.
fn feed(pmtk: &mut PmtkPlugin, pq: &mut PqPlugin, sentence: &str) -> Result<Statement, Error> {
    let mut registry = PluginRegistry::new();
    registry.register(pmtk).unwrap();
    registry.register(pq).unwrap();
    Dispatcher::new(registry).feed_sentence(sentence)
}

#[test]
fn pmtk_command_round_trip() {
    // Build the search mode command the GPS task sends at startup, then feed
    // the module's acknowledgement of it back through the parser.
    let command = PmtkCommand::new(PMTK_API_SET_GNSS_SEARCH_MODE, &["1", "1", "1", "0", "0"])
        .expect("command fits");
    assert_eq!(command.as_str(), l96::SEARCH_GPS_GLONASS_GALILEO);

    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();

    let reply = "$PMTK001,353,3,1,1,1,0,0,15*00\r\n";
    assert_eq!(feed(&mut pmtk, &mut pq, reply), Ok(Statement::Plugin(0)));

    let ack = pmtk.ack().expect("acknowledgement recorded");
    assert_eq!(ack.packet_type, PMTK_API_SET_GNSS_SEARCH_MODE);
    assert_eq!(ack.flag, Some(PmtkAckFlag::ActionSucceeded));
    assert_eq!(
        pmtk.search_mode(),
        GnssSearchMode {
            gps: true,
            glonass: true,
            galileo: true,
            galileo_full: false,
            beidou: false,
        },
    );
}

#[test]
fn pmtk_command_matches_every_l96_pmtk_literal() {
    assert_eq!(
        PmtkCommand::new(PMTK_CMD_STANDBY_MODE, &["0"])
            .unwrap()
            .as_str(),
        l96::ENTER_STANDBY,
    );
    assert_eq!(
        PmtkCommand::new(225, &["0"]).unwrap().as_str(),
        l96::ENTER_FULL_ON,
    );
    assert_eq!(
        PmtkCommand::new(225, &["8"]).unwrap().as_str(),
        l96::ENTER_ALWAYS_LOCATE,
    );
    // Three digit padding, as the acknowledgement literal is written.
    assert_eq!(
        PmtkCommand::new(1, &["225", "3"]).unwrap().as_str(),
        l96::REPLY_ALWAYS_LOCATE,
    );
    assert_eq!(
        PmtkCommand::new(286, &["1"]).unwrap().as_str(),
        l96::AIC_ENABLE,
    );
    assert_eq!(
        PmtkCommand::new(353, &["0", "1", "0", "0", "0"])
            .unwrap()
            .as_str(),
        l96::SEARCH_GLONASS,
    );
}

#[test]
fn every_l96_command_has_a_valid_checksum() {
    for command in l96::ALL {
        assert!(verify(command), "bad checksum: {command:?}");
        assert!(command.ends_with("\r\n"), "no line ending: {command:?}");
    }
}

#[test]
fn pmtk_command_rejects_an_overlong_command() {
    let long = "0123456789".repeat(10);
    assert!(long.len() > PMTK_COMMAND_MAX);
    assert_eq!(
        PmtkCommand::new(353, &[long.as_str()]),
        Err(Error::InvalidArg),
    );
}

#[test]
fn pmtk_reports_the_acknowledgement_flag_as_an_error() {
    for (flag, expected) in [
        ("0", Err(Error::InvalidArg)),
        ("1", Err(Error::NotSupported)),
        ("2", Err(Error::Failed)),
        ("3", Ok(Statement::Plugin(0))),
    ] {
        let mut pmtk = PmtkPlugin::new();
        let mut pq = PqPlugin::new();

        let sentence = PmtkCommand::new(1, &["161", flag]).unwrap();
        assert_eq!(feed(&mut pmtk, &mut pq, sentence.as_str()), expected);
        assert_eq!(
            pmtk.ack().and_then(|ack| ack.flag),
            PmtkAckFlag::from_field(flag.parse().unwrap()),
        );
    }
}

#[test]
fn pmtk_declines_an_acknowledgement_for_an_unhandled_packet_type() {
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();

    // 604 has no entry in the `message_parser` table.
    assert_eq!(
        feed(&mut pmtk, &mut pq, "$PMTK001,604,3*32\r\n"),
        Err(Error::NotSupported),
    );
    // The sentence still belongs to the PMTK plugin, which goes on to read the
    // flag out of field 2.
    assert_eq!(
        pmtk.ack().and_then(|ack| ack.flag),
        Some(PmtkAckFlag::ActionSucceeded),
    );
}

#[test]
fn pmtk_reads_system_and_text_messages() {
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();

    assert_eq!(
        feed(&mut pmtk, &mut pq, "$PMTK010,001*2E\r\n"),
        Ok(Statement::Plugin(0)),
    );
    assert_eq!(pmtk.system_message(), Some("Startup"));

    assert_eq!(
        feed(&mut pmtk, &mut pq, "$PMTK011,MTKGPS*08\r\n"),
        Ok(Statement::Plugin(0)),
    );
    assert_eq!(pmtk.text_message(), "MTKGPS");
    // `pmtk_parse()` clears the message number after a text message.
    assert_eq!(pmtk.message_number(), 0);
}

#[test]
fn pmtk_system_message_outside_the_table_is_not_read_out_of_bounds() {
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();

    assert_eq!(
        feed(&mut pmtk, &mut pq, "$PMTK010,099*2F\r\n"),
        Ok(Statement::Plugin(0)),
    );
    assert_eq!(pmtk.system_message_id(), Some(99));
    assert_eq!(pmtk.system_message(), None);
}

#[test]
fn pq_parses_a_glp_reply() {
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();

    // The module's answer to L96_ENTER_GLP.
    assert_eq!(
        feed(&mut pmtk, &mut pq, l96::REPLY_GLP),
        Ok(Statement::Plugin(1)),
    );
    assert_eq!(pq.statement(), Some(PqStatement::PqGlp));
    let glp = pq.glp().expect("value recorded");
    assert_eq!(glp.access, GlpAccess::Write);
    assert_eq!(glp.value(), "OK");
}

#[test]
fn pq_reads_a_glp_query_as_a_read() {
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();

    assert_eq!(
        feed(&mut pmtk, &mut pq, "$PQGLP,R,1,1*24\r\n"),
        Ok(Statement::Plugin(1))
    );
    let glp = pq.glp().expect("value recorded");
    assert_eq!(glp.access, GlpAccess::Read);
    assert_eq!(glp.value(), "1");
}

#[test]
fn pq_claims_every_statement_it_recognises() {
    for statement in PqStatement::ALL {
        let mut pq = PqPlugin::new();
        let identifier = format!("${}", statement.marker());
        pq.detect(&SentenceItem::new(0, &identifier))
            .unwrap_or_else(|err| panic!("{identifier} declined: {err:?}"));
        assert_eq!(pq.statement(), Some(statement));
        // A statement other than PQGLP has no field the parser reads.
        assert_eq!(pq.parse(&SentenceItem::new(1, "W")), Ok(()));
    }
}

#[test]
fn pq_declines_a_sentence_before_any_was_detected() {
    let mut pq = PqPlugin::new();
    assert_eq!(
        pq.parse(&SentenceItem::new(1, "W")),
        Err(Error::NotSupported)
    );
}

#[test]
fn a_sentence_no_plugin_claims_is_unknown_and_does_not_fail() {
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();

    // Neither PMTK nor PQ, and not one of the six built in statements.
    assert_eq!(
        feed(
            &mut pmtk,
            &mut pq,
            "$GPZDA,172809.456,12,07,1996,00,00*57\r\n"
        ),
        Ok(Statement::Unknown),
    );
    assert_eq!(pmtk.message_number(), 0);
    assert_eq!(pq.statement(), None);
    assert_eq!(pq.glp(), None);
}

#[test]
fn a_built_in_statement_is_never_offered_to_a_plugin() {
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();

    for (sentence, expected) in [
        (
            "$GPGGA,161229.487,3723.2475,N,12158.3416,W,1,07,1.0,9.0,M,,,,0000*18\r\n",
            Statement::Gga,
        ),
        (
            "$GPGSA,A,3,07,02,26,27,09,04,15,,,,,,1.8,1.0,1.5*33\r\n",
            Statement::Gsa,
        ),
        (
            "$GPRMC,161229.487,A,3723.2475,N,12158.3416,W,0.13,309.62,120598,,*10\r\n",
            Statement::Rmc,
        ),
        (
            "$GPGSV,2,1,07,07,79,048,42,02,51,062,43,26,36,256,42,27,27,138,42*71\r\n",
            Statement::Gsv,
        ),
        (
            "$GPGLL,3723.2475,N,12158.3416,W,161229.487,A*2C\r\n",
            Statement::Gll,
        ),
        ("$GPVTG,309.62,T,,M,0.13,N,0.2,K*6E\r\n", Statement::Vtg),
    ] {
        assert_eq!(feed(&mut pmtk, &mut pq, sentence), Ok(expected));
    }
    assert_eq!(pq.statement(), None);
    assert_eq!(pmtk.message_number(), 0);
}

#[test]
fn the_registry_dispatches_in_registration_order() {
    // Swapping the registration order swaps the plugin indices, exactly as
    // swapping the entries of `nmea_parser_config_t::plugins` would.
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();
    let mut registry = PluginRegistry::new();
    registry.register(&mut pq).unwrap();
    registry.register(&mut pmtk).unwrap();
    let mut dispatcher = Dispatcher::new(registry);

    assert_eq!(
        dispatcher.feed_sentence(l96::REPLY_GLP),
        Ok(Statement::Plugin(0))
    );
    assert_eq!(
        dispatcher.feed_sentence("$PMTK001,161,3*36\r\n"),
        Ok(Statement::Plugin(1)),
    );
}

#[test]
fn the_registry_holds_gps_max_parser_plugins_plugins() {
    let mut pmtk = PmtkPlugin::new();
    let mut pq = PqPlugin::new();
    let mut extra = PqPlugin::new();
    let mut registry = PluginRegistry::new();

    assert_eq!(GPS_MAX_PARSER_PLUGINS, 2);
    assert!(registry.is_empty());
    registry.register(&mut pmtk).unwrap();
    registry.register(&mut pq).unwrap();
    assert_eq!(registry.len(), GPS_MAX_PARSER_PLUGINS);
    assert_eq!(registry.register(&mut extra), Err(Error::RegistryFull));
}

#[test]
fn an_empty_registry_leaves_every_vendor_sentence_unclaimed() {
    let mut dispatcher = Dispatcher::new(PluginRegistry::new());
    assert_eq!(
        dispatcher.feed_sentence(l96::REPLY_GLP),
        Ok(Statement::Unknown),
    );
    assert_eq!(
        dispatcher.feed_sentence("$PMTK001,353,3*35\r\n"),
        Ok(Statement::Unknown),
    );
}

#[test]
fn plugin_statements_keep_the_c_statement_ids() {
    assert_eq!(Statement::Unknown.as_raw(), 0);
    assert_eq!(Statement::Vtg.as_raw(), STATEMENT_PLUGIN - 1);
    for index in 0..GPS_MAX_PARSER_PLUGINS as u8 {
        let raw = STATEMENT_PLUGIN + index;
        assert_eq!(Statement::Plugin(index).as_raw(), raw);
        assert_eq!(Statement::from_raw(raw), Statement::Plugin(index));
    }
}
