//! Byte-at-a-time XML pull lexer.
//!
//! Replaces the `lib/sxml` git submodule that `lib/Platinenmacher/parser/gpx.c`
//! used. sxml needed the interesting part of the document to sit in one buffer
//! and asked the caller to rewind and retry on `SXML_ERROR_BUFFERDRY`; this
//! lexer consumes one byte at a time, so a document can arrive in arbitrary
//! chunks split mid-tag or mid-attribute without any rewinding.
//!
//! Only the subset of XML that GPX files use is recognised: elements,
//! attributes, character data, CDATA sections, comments, processing
//! instructions and a doctype. Entity references are passed through verbatim,
//! which is what sxml did.

use crate::buf::Buf;
use crate::Error;

/// Longest element or attribute name we keep. Longer names overflow their
/// buffer and therefore match nothing, i.e. they are skipped.
const MAX_NAME: usize = 32;
/// Longest attribute value we keep (`lat`/`lon` are ~10 characters).
const MAX_VALUE: usize = 32;
/// Longest character-data run we keep (elevation values, track names).
pub(crate) const MAX_TEXT: usize = 64;

/// What the lexer just finished reading.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Event {
    /// `<trkpt ...>` — [`Lexer::name`] is set.
    Start,
    /// `<trkpt ... />` — a start tag and an end tag in one.
    Empty,
    /// `</trkpt>` — [`Lexer::name`] is set.
    End,
    /// `lat="49.6"` — [`Lexer::attr_name`], [`Lexer::attr_value`] and the
    /// enclosing [`Lexer::name`] are set.
    Attribute,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum State {
    /// Character data between tags.
    Text,
    /// Consumed `<`.
    TagStart,
    /// Reading a start-tag name.
    StartName,
    /// Inside a start tag, expecting an attribute name, `/` or `>`.
    InTag,
    /// Reading an attribute name.
    AttrName,
    /// Read an attribute name and whitespace, expecting `=`.
    AfterAttrName,
    /// Consumed `=`, expecting the opening quote.
    BeforeAttrValue,
    /// Inside a quoted attribute value.
    AttrValue,
    /// Consumed `/` inside a start tag, expecting `>`.
    SelfClose,
    /// Reading an end-tag name.
    EndName,
    /// Read an end-tag name and whitespace, expecting `>`.
    AfterEndName,
    /// Inside `<? ... ?>`.
    Pi,
    /// Consumed `?` inside a processing instruction.
    PiEnd,
    /// Consumed `<!`, deciding between comment, CDATA and doctype.
    Bang,
    /// Consumed `<!-`, expecting the second `-`.
    CommentStart,
    /// Inside `<!-- ... -->`.
    Comment,
    /// Consumed one `-` inside a comment.
    CommentDash1,
    /// Consumed `--` inside a comment.
    CommentDash2,
    /// Matching the rest of `<![CDATA[`.
    CDataStart,
    /// Inside `<![CDATA[ ... ]]>`.
    CData,
    /// Consumed one `]` inside a CDATA section.
    CDataBracket1,
    /// Consumed `]]` inside a CDATA section.
    CDataBracket2,
    /// Inside `<!DOCTYPE ... >`, counting `[` / `]` nesting.
    Doctype,
}

const CDATA_INTRO: &[u8] = b"[CDATA[";

fn is_name_start(b: u8) -> bool {
    b.is_ascii_alphabetic() || b == b'_' || b == b':' || b >= 0x80
}

fn is_name_char(b: u8) -> bool {
    is_name_start(b) || b.is_ascii_digit() || b == b'-' || b == b'.'
}

fn is_space(b: u8) -> bool {
    matches!(b, b' ' | b'\t' | b'\r' | b'\n')
}

/// Incremental XML lexer. Feed it bytes with [`Lexer::push`].
pub(crate) struct Lexer {
    state: State,
    name: Buf<MAX_NAME>,
    attr_name: Buf<MAX_NAME>,
    attr_value: Buf<MAX_VALUE>,
    text: Buf<MAX_TEXT>,
    /// Quote character that opened the attribute value currently being read.
    quote: u8,
    /// How much of `CDATA_INTRO` has been matched so far.
    intro: usize,
    /// `[` / `]` nesting depth inside a doctype.
    doctype_depth: u32,
    /// Whether character data is worth keeping. The caller only switches this
    /// on inside the few elements it reads, which is what bounds `text`.
    capture_text: bool,
}

impl Lexer {
    pub(crate) const fn new() -> Self {
        Self {
            state: State::Text,
            name: Buf::new(),
            attr_name: Buf::new(),
            attr_value: Buf::new(),
            text: Buf::new(),
            quote: b'"',
            intro: 0,
            doctype_depth: 0,
            capture_text: false,
        }
    }

    pub(crate) fn name(&self) -> &Buf<MAX_NAME> {
        &self.name
    }

    pub(crate) fn attr_name(&self) -> &Buf<MAX_NAME> {
        &self.attr_name
    }

    pub(crate) fn attr_value(&self) -> &Buf<MAX_VALUE> {
        &self.attr_value
    }

    pub(crate) fn text(&self) -> &Buf<MAX_TEXT> {
        &self.text
    }

    /// Starts collecting character data into [`Lexer::text`] from a clean slate.
    pub(crate) fn capture_text(&mut self) {
        self.text.clear();
        self.capture_text = true;
    }

    pub(crate) fn stop_capture_text(&mut self) {
        self.capture_text = false;
    }

    /// Feeds one byte, returning the event it completed, if any.
    pub(crate) fn push(&mut self, b: u8) -> Result<Option<Event>, Error> {
        match self.state {
            State::Text => {
                if b == b'<' {
                    self.state = State::TagStart;
                } else if self.capture_text {
                    self.text.push(b);
                }
            }
            State::TagStart => match b {
                b'/' => {
                    self.name.clear();
                    self.state = State::EndName;
                }
                b'?' => self.state = State::Pi,
                b'!' => {
                    self.intro = 0;
                    self.state = State::Bang;
                }
                _ if is_name_start(b) => {
                    self.name.clear();
                    self.name.push(b);
                    self.state = State::StartName;
                }
                // `<` followed by whitespace, `>` or punctuation. This is the
                // malformed `<trkseg` / `<trkpt` case in test_error.gpx.
                _ => return Err(Error::Malformed),
            },
            State::StartName => match b {
                b'>' => {
                    self.state = State::Text;
                    return Ok(Some(Event::Start));
                }
                b'/' => self.state = State::SelfClose,
                _ if is_space(b) => self.state = State::InTag,
                _ if is_name_char(b) => self.name.push(b),
                _ => return Err(Error::Malformed),
            },
            State::InTag => match b {
                b'>' => {
                    self.state = State::Text;
                    return Ok(Some(Event::Start));
                }
                b'/' => self.state = State::SelfClose,
                _ if is_space(b) => {}
                _ if is_name_start(b) => {
                    self.attr_name.clear();
                    self.attr_name.push(b);
                    self.state = State::AttrName;
                }
                // Another `<` before this tag was closed, or a stray `=`.
                _ => return Err(Error::Malformed),
            },
            State::AttrName => match b {
                b'=' => self.state = State::BeforeAttrValue,
                _ if is_space(b) => self.state = State::AfterAttrName,
                _ if is_name_char(b) => self.attr_name.push(b),
                _ => return Err(Error::Malformed),
            },
            State::AfterAttrName => match b {
                b'=' => self.state = State::BeforeAttrValue,
                _ if is_space(b) => {}
                // XML has no valueless attributes.
                _ => return Err(Error::Malformed),
            },
            State::BeforeAttrValue => match b {
                b'"' | b'\'' => {
                    self.quote = b;
                    self.attr_value.clear();
                    self.state = State::AttrValue;
                }
                _ if is_space(b) => {}
                _ => return Err(Error::Malformed),
            },
            State::AttrValue => {
                if b == self.quote {
                    self.state = State::InTag;
                    return Ok(Some(Event::Attribute));
                }
                if b == b'<' {
                    return Err(Error::Malformed);
                }
                self.attr_value.push(b);
            }
            State::SelfClose => {
                if b != b'>' {
                    return Err(Error::Malformed);
                }
                self.state = State::Text;
                return Ok(Some(Event::Empty));
            }
            State::EndName => match b {
                b'>' => {
                    self.state = State::Text;
                    return Ok(Some(Event::End));
                }
                _ if is_space(b) => self.state = State::AfterEndName,
                _ if is_name_char(b) => self.name.push(b),
                _ => return Err(Error::Malformed),
            },
            State::AfterEndName => match b {
                b'>' => {
                    self.state = State::Text;
                    return Ok(Some(Event::End));
                }
                _ if is_space(b) => {}
                _ => return Err(Error::Malformed),
            },
            State::Pi => {
                if b == b'?' {
                    self.state = State::PiEnd;
                }
            }
            State::PiEnd => match b {
                b'>' => self.state = State::Text,
                b'?' => {}
                _ => self.state = State::Pi,
            },
            State::Bang => match b {
                b'-' => self.state = State::CommentStart,
                b'[' => {
                    self.intro = 1;
                    self.state = State::CDataStart;
                }
                // `<!DOCTYPE ...`
                _ => {
                    self.doctype_depth = 0;
                    self.state = State::Doctype;
                }
            },
            State::CommentStart => {
                if b != b'-' {
                    return Err(Error::Malformed);
                }
                self.state = State::Comment;
            }
            State::Comment => {
                if b == b'-' {
                    self.state = State::CommentDash1;
                }
            }
            State::CommentDash1 => {
                self.state = if b == b'-' {
                    State::CommentDash2
                } else {
                    State::Comment
                };
            }
            State::CommentDash2 => match b {
                b'>' => self.state = State::Text,
                b'-' => {}
                _ => self.state = State::Comment,
            },
            State::CDataStart => {
                if b != CDATA_INTRO[self.intro] {
                    return Err(Error::Malformed);
                }
                self.intro += 1;
                if self.intro == CDATA_INTRO.len() {
                    self.state = State::CData;
                }
            }
            State::CData => {
                if b == b']' {
                    self.state = State::CDataBracket1;
                } else if self.capture_text {
                    self.text.push(b);
                }
            }
            State::CDataBracket1 => {
                if b == b']' {
                    self.state = State::CDataBracket2;
                } else {
                    // A lone `]` was content after all.
                    self.push_cdata_content(&[b']', b]);
                    self.state = State::CData;
                }
            }
            State::CDataBracket2 => match b {
                b'>' => self.state = State::Text,
                // `]]]>` — the first `]` was content.
                b']' => self.push_cdata_content(b"]"),
                _ => {
                    self.push_cdata_content(&[b']', b']', b]);
                    self.state = State::CData;
                }
            },
            State::Doctype => match b {
                b'[' => self.doctype_depth += 1,
                b']' => self.doctype_depth = self.doctype_depth.saturating_sub(1),
                b'>' if self.doctype_depth == 0 => self.state = State::Text,
                _ => {}
            },
        }
        Ok(None)
    }

    fn push_cdata_content(&mut self, bytes: &[u8]) {
        if self.capture_text {
            for &b in bytes {
                self.text.push(b);
            }
        }
    }
}
