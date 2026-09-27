//! Framebuffer display and drawing primitives.
//!
//! Replaces `lib/Platinenmacher/display.h` and `lib/Platinenmacher/display.c`.
//!
//! The C `display_t` carried three function pointers (`write_pixel`,
//! `decompress`, `update`) that the panel driver filled in. They are replaced
//! by the [`DisplayTarget`] trait, which [`Display`] is generic over. The
//! framebuffer is owned by [`Display`] instead of being allocated by the
//! driver, so `display_init()` becomes [`Display::new()`].
//!
//! Deviations from the C code, all of them deliberate:
//!
//! * `display_pixel_draw()` returned `PM_FAIL` when `dsp->write_pixel` was
//!   `NULL` and ignored the return value of the callback. A trait method always
//!   exists, so [`Display::pixel_draw()`] propagates what the target reports
//!   instead; a target that cannot write reports [`Error::Fail`].
//! * `display_draw_raw_rot()` read `img` without a length check. Here a short
//!   buffer is reported as [`Error::OutOfBounds`] rather than read past its end.
//! * Intermediate coordinate arithmetic uses `i32` where the C code used `int`
//!   (`int16_t` promoted), and results are truncated to `i16` at the same
//!   points the C compiler truncated them.
//!
//! `display_text_draw()`/`display_text_draw_len()` are ported with the font
//! code (pm-font); they build on [`Display::draw_raw_rot()`].

use alloc::vec;
use alloc::vec::Vec;

use crate::colors::Color;
use crate::error::{Error, Result};
use crate::geometric::Rect;

/// Port of `display_rotation_t`, rotation clockwise.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Hash)]
#[repr(u8)]
pub enum Rotation {
    /// `DISPLAY_ROTATE_0`
    #[default]
    Deg0 = 0,
    /// `DISPLAY_ROTATE_90`
    Deg90 = 1,
    /// `DISPLAY_ROTATE_180`
    Deg180 = 2,
    /// `DISPLAY_ROTATE_270`
    Deg270 = 3,
}

/// Port of the inline `sizeof_fb()` of `display.h`: the size in bytes of a
/// framebuffer for `size` at `bpp` bits per pixel.
pub const fn sizeof_fb(size: Rect, bpp: u8) -> usize {
    (size.width as usize * size.height as usize * bpp as usize) / 8
}

/// The framebuffer of a [`Display`] plus its geometry, handed to the target.
///
/// This is what the C code reached for through `const display_t *dsp` inside
/// `write_pixel`: `dsp->fb`, `dsp->size`, `dsp->bpp` and `dsp->rotation`.
pub struct FrameBuffer<'a> {
    /// `dsp->fb`, `dsp->fb_size` bytes long
    pub data: &'a mut [u8],
    /// `dsp->size`
    pub size: Rect,
    /// `dsp->bpp`
    pub bpp: u8,
    /// `dsp->rotation`
    pub rotation: Rotation,
}

/// The panel behind a [`Display`]: replaces the `write_pixel`, `decompress`
/// and `update` function pointers of `struct display`.
pub trait DisplayTarget {
    /// Replaces `error_code_t (*write_pixel)(const display_t *, int16_t, int16_t, uint8_t)`.
    ///
    /// `x`/`y` are guaranteed to be inside `fb.size`; the target decides how a
    /// pixel is packed into the framebuffer and applies `fb.rotation`.
    fn write_pixel(&mut self, fb: FrameBuffer<'_>, x: i16, y: i16, color: Color) -> Result<()>;

    /// Replaces `uint8_t (*decompress)(rect_t *, int16_t, int16_t, const uint8_t *)`:
    /// the color of pixel `x`,`y` of the image `data` drawn into `size`.
    fn decompress(&self, size: &Rect, x: i16, y: i16, data: &[u8]) -> Color;

    /// Replaces `void (*update)()`: push the framebuffer to the hardware.
    fn update(&mut self, fb: &[u8]) -> Result<()>;
}

/// Port of `struct display`.
pub struct Display<T: DisplayTarget> {
    size: Rect,
    fb: Vec<u8>,
    bpp: u8,
    rotation: Rotation,
    target: T,
}

impl<T: DisplayTarget> Display<T> {
    /// Port of `display_init()`, including the framebuffer the C callers
    /// allocated themselves right after it: `size.width * size.height * bpp / 8`
    /// zeroed bytes.
    pub fn new(width: u16, height: u16, bpp: u8, rotation: Rotation, target: T) -> Self {
        let size = Rect::new(0, 0, width, height);
        Self {
            size,
            fb: vec![0u8; sizeof_fb(size, bpp)],
            bpp,
            rotation,
            target,
        }
    }

    /// `dsp->size`
    pub const fn size(&self) -> Rect {
        self.size
    }

    /// `dsp->bpp`
    pub const fn bpp(&self) -> u8 {
        self.bpp
    }

    /// `dsp->rotation`
    pub const fn rotation(&self) -> Rotation {
        self.rotation
    }

    pub fn set_rotation(&mut self, rotation: Rotation) {
        self.rotation = rotation;
    }

    /// `dsp->fb`
    pub fn fb(&self) -> &[u8] {
        &self.fb
    }

    /// `dsp->fb`, writable for panel drivers that fill it directly.
    pub fn fb_mut(&mut self) -> &mut [u8] {
        &mut self.fb
    }

    /// `dsp->fb_size`
    pub fn fb_size(&self) -> usize {
        self.fb.len()
    }

    pub const fn target(&self) -> &T {
        &self.target
    }

    pub fn target_mut(&mut self) -> &mut T {
        &mut self.target
    }

    /// Port of `display_commit_fb()`: commit the framebuffer to the hardware.
    ///
    /// The C version ignored what `update()` reported; here it is propagated.
    pub fn commit_fb(&mut self) -> Result<()> {
        let Self { fb, target, .. } = self;
        target.update(fb)
    }

    /// Port of `display_fill()`: fill every pixel with `color`.
    ///
    /// Always `Ok(())`, like the C version, which ignored what the individual
    /// pixel draws reported.
    pub fn fill(&mut self, color: Color) -> Result<()> {
        for x in 0..self.size.width as i32 {
            for y in 0..self.size.height as i32 {
                let _ = self.pixel_draw(x as i16, y as i16, color);
            }
        }
        Ok(())
    }

    /// Port of `display_pixel_draw()`: draw one pixel with `color` at `x`,`y`.
    ///
    /// * [`Error::OutOfBounds`] if the pixel is outside the display
    /// * `Ok(())` without touching the framebuffer for [`Color::Transparent`]
    /// * whatever the target reports otherwise (the C version answered
    ///   `PM_FAIL` here when no `write_pixel` was installed)
    pub fn pixel_draw(&mut self, x: i16, y: i16, color: Color) -> Result<()> {
        if x < 0
            || y < 0
            || x as i32 >= self.size.width as i32
            || y as i32 >= self.size.height as i32
        {
            return Err(Error::OutOfBounds);
        }

        if color == Color::Transparent {
            return Ok(());
        }

        let Self {
            size,
            fb,
            bpp,
            rotation,
            target,
        } = self;
        target.write_pixel(
            FrameBuffer {
                data: fb.as_mut_slice(),
                size: *size,
                bpp: *bpp,
                rotation: *rotation,
            },
            x,
            y,
            color,
        )
    }

    /// Port of `display_rect_draw()`: outline of the rectangle at `x`,`y`,
    /// drawn with four lines. Always `Ok(())`, like the C version.
    pub fn rect_draw(
        &mut self,
        x: i16,
        y: i16,
        width: u16,
        height: u16,
        color: Color,
    ) -> Result<()> {
        // line at start coordinate counts as one
        let height = height.wrapping_sub(1) as i32;
        let width = width.wrapping_sub(1) as i32;
        let right = (x as i32 + width) as i16;
        let bottom = (y as i32 + height) as i16;

        let _ = self.line_draw(x, y, x, bottom, color);
        let _ = self.line_draw(x, bottom, right, bottom, color);
        let _ = self.line_draw(right, bottom, right, y, color);
        let _ = self.line_draw(right, y, x, y, color);

        Ok(())
    }

    /// Port of the static `display_line_draw_low()`.
    fn line_draw_low(&mut self, x1: i16, y1: i16, x2: i16, y2: i16, color: Color) {
        let d_x = x2 as i32 - x1 as i32;
        let mut d_y = y2 as i32 - y1 as i32;
        let mut yi = 1;

        if d_y < 0 {
            yi = -1;
            d_y = -d_y;
        }

        let mut d = (2 * d_y) - d_x;
        let mut y = y1 as i32;
        for x in (x1 as i32)..=(x2 as i32) {
            let _ = self.pixel_draw(x as i16, y as i16, color);
            if d > 0 {
                y += yi;
                d -= 2 * d_x;
            }
            d += 2 * d_y;
        }
    }

    /// Port of the static `display_line_draw_height()`.
    fn line_draw_height(&mut self, x1: i16, y1: i16, x2: i16, y2: i16, color: Color) {
        let mut d_x = x2 as i32 - x1 as i32;
        let d_y = y2 as i32 - y1 as i32;
        let mut xi = 1;

        if d_x < 0 {
            xi = -1;
            d_x = -d_x;
        }

        let mut d = (2 * d_x) - d_y;
        let mut x = x1 as i32;
        for y in (y1 as i32)..=(y2 as i32) {
            let _ = self.pixel_draw(x as i16, y as i16, color);
            if d > 0 {
                x += xi;
                d -= 2 * d_y;
            }
            d += 2 * d_x;
        }
    }

    /// Port of `display_line_draw()`: line from `x1`,`y1` to `x2`,`y2`.
    ///
    /// [`Error::OutOfBounds`] if any endpoint is outside the display; the line
    /// is drawn regardless, clipped pixel by pixel, exactly as in C.
    pub fn line_draw(
        &mut self,
        mut x1: i16,
        mut y1: i16,
        mut x2: i16,
        mut y2: i16,
        color: Color,
    ) -> Result<()> {
        let width = self.size.width as i32;
        let height = self.size.height as i32;
        let mut ret = Ok(());
        if x1 < 0
            || x2 < 0
            || y1 < 0
            || y2 < 0
            || x1 as i32 >= width
            || x2 as i32 >= width
            || y1 as i32 >= height
            || y2 as i32 >= height
        {
            ret = Err(Error::OutOfBounds);
        }

        if x1 == x2 {
            // straight line in y direction
            if y1 > y2 {
                // flip direction
                core::mem::swap(&mut y1, &mut y2);
            }
            for y in (y1 as i32)..=(y2 as i32) {
                let _ = self.pixel_draw(x1, y as i16, color);
            }
            return ret;
        }
        if y1 == y2 {
            // straight line in x direction
            if x1 > x2 {
                // flip direction
                core::mem::swap(&mut x1, &mut x2);
            }
            for x in (x1 as i32)..=(x2 as i32) {
                let _ = self.pixel_draw(x as i16, y1, color);
            }
            return ret;
        }

        // case for line going skewed
        if (y2 as i32 - y1 as i32).abs() < (x2 as i32 - x1 as i32).abs() {
            if x1 > x2 {
                self.line_draw_low(x2, y2, x1, y1, color);
            } else {
                self.line_draw_low(x1, y1, x2, y2, color);
            }
        } else if y1 > y2 {
            self.line_draw_height(x2, y2, x1, y1, color);
        } else {
            self.line_draw_height(x1, y1, x2, y2, color);
        }

        ret
    }

    /// Port of `display_circle_fill()`: concentric circles around `x0`,`y0`.
    ///
    /// Keeps the holes the C algorithm leaves in the filled area; see the
    /// ignored test in this module.
    pub fn circle_fill(&mut self, x0: i16, y0: i16, r: u16, color: Color) -> Result<()> {
        let _ = self.pixel_draw(x0, y0, color);
        for r1 in 1..=r {
            let _ = self.circle_draw(x0, y0, r1, color);
        }
        Ok(())
    }

    /// Port of `display_circle_draw()`: full circle of radius `r`.
    pub fn circle_draw(&mut self, x0: i16, y0: i16, r: u16, color: Color) -> Result<()> {
        self.circle_draw_segment(x0, y0, r, color, 0xff)
    }

    /// Port of `display_circle_draw_segment()`.
    ///
    /// `segment` selects eighths of the circle, bit 0 being 12 to 1:30 o'clock
    /// and then clockwise.
    pub fn circle_draw_segment(
        &mut self,
        x0: i16,
        y0: i16,
        r: u16,
        color: Color,
        segment: u8,
    ) -> Result<()> {
        let mut x = r as i32 - 1;
        let mut y = 0i32;
        let mut d_x = 1i32;
        let mut d_y = 1i32;
        let mut err = d_x - ((r as i32) << 1); // radius / 2

        while x >= y {
            let plot = |dsp: &mut Self, dx: i32, dy: i32| {
                let _ = dsp.pixel_draw((x0 as i32 + dx) as i16, (y0 as i32 + dy) as i16, color);
            };
            if segment & 1 != 0 {
                plot(self, y, -x); // 12 Uhr - 1.5 Uhr
            }
            if segment & 2 != 0 {
                plot(self, x, -y); // 1.5 Uhr - 3 Uhr
            }
            if segment & 4 != 0 {
                plot(self, x, y); // 3 - 4.5 Uhr
            }
            if segment & 8 != 0 {
                plot(self, y, x); // 4.5 - 6 Uhr
            }
            if segment & 16 != 0 {
                plot(self, -y, x); // 6 - 7.5 Uhr
            }
            if segment & 32 != 0 {
                plot(self, -x, y); // 7.5 - 9 Uhr
            }
            if segment & 64 != 0 {
                plot(self, -x, -y); // 9 - 10.5 Uhr
            }
            if segment & 128 != 0 {
                plot(self, -y, -x); // 10.5 - 12 Uhr
            }

            if err <= 0 {
                y += 1;
                err += d_y;
                d_y += 2;
            }

            if err > 0 {
                x -= 1;
                d_x += 2;
                err += d_x - ((r as i32) << 1); // radius / 2
            }
        }

        Ok(())
    }

    /// Port of `display_rect_fill()`: filled rectangle. Always `Ok(())`.
    pub fn rect_fill(
        &mut self,
        x0: i16,
        y0: i16,
        width: u16,
        height: u16,
        color: Color,
    ) -> Result<()> {
        for x in 0..width as i32 {
            for y in 0..height as i32 {
                let _ = self.pixel_draw((x0 as i32 + x) as i16, (y0 as i32 + y) as i16, color);
            }
        }
        Ok(())
    }

    /// Port of `display_draw_raw_rot()`: 1 bit per pixel bitmap, set bits in
    /// `color1` and clear bits in `color2`.
    ///
    /// With [`Rotation::Deg90`] a byte of `img` is a row, otherwise a column.
    /// Unlike the C version this reports [`Error::OutOfBounds`] instead of
    /// reading past the end of a too short `img`.
    // the parameter list is the one of display_draw_raw_rot()
    #[allow(clippy::too_many_arguments)]
    pub fn draw_raw_rot(
        &mut self,
        img: &[u8],
        x0: i16,
        y0: i16,
        width: u16,
        height: u16,
        color1: Color,
        color2: Color,
        rot: Rotation,
    ) -> Result<()> {
        let bytes = (width as u32 * height as u32) / 8;
        if bytes as usize > img.len() {
            return Err(Error::OutOfBounds);
        }

        for p in 0..bytes {
            // offset for picture size bytes in front of image data
            let field = img[p as usize];
            for i in 0..8u32 {
                let color = if field & (1 << i) != 0 {
                    color1
                } else {
                    color2
                };
                let (x, y) = if rot == Rotation::Deg90 {
                    (
                        x0 as i32 + ((p / width as u32) * 8 + i) as i32,
                        y0 as i32 + (p % width as u32) as i32,
                    )
                } else {
                    (
                        x0 as i32 + (p % width as u32) as i32,
                        y0 as i32 + ((p / width as u32) * 8 + i) as i32,
                    )
                };
                let _ = self.pixel_draw(x as i16, y as i16, color);
            }
        }
        Ok(())
    }

    /// Port of `display_draw_raw()`: [`Self::draw_raw_rot()`] without rotation.
    // the parameter list is the one of display_draw_raw()
    #[allow(clippy::too_many_arguments)]
    pub fn draw_raw(
        &mut self,
        img: &[u8],
        x0: i16,
        y0: i16,
        width: u16,
        height: u16,
        color1: Color,
        color2: Color,
    ) -> Result<()> {
        self.draw_raw_rot(img, x0, y0, width, height, color1, color2, Rotation::Deg0)
    }

    /// Port of `display_draw_image()`: `w` by `h` image at `x`,`y`, expanded
    /// pixel by pixel through [`DisplayTarget::decompress()`].
    ///
    /// [`Error::OutOfBounds`] if the top left corner is outside the display;
    /// the image is drawn regardless, clipped pixel by pixel, as in C.
    pub fn draw_image(&mut self, data: &[u8], x: i16, y: i16, w: u16, h: u16) -> Result<()> {
        let mut ret = Ok(());
        if x < 0
            || y < 0
            || x as i32 >= self.size.width as i32
            || y as i32 >= self.size.height as i32
        {
            ret = Err(Error::OutOfBounds);
        }

        let image_box = Rect::new(x, y, w, h);

        for y0 in 0..h as i32 {
            for x0 in 0..w as i32 {
                let color = self
                    .target
                    .decompress(&image_box, x0 as i16, y0 as i16, data);
                let _ = self.pixel_draw((x0 + x as i32) as i16, (y0 + y as i32) as i16, color);
            }
        }

        ret
    }
}

#[cfg(test)]
mod tests {
    //! Ports `test/host/Platinenmacher/test_display/test.c`. The `picture`
    //! arrays of that file are reused unchanged as the expected framebuffers,
    //! so the Rust primitives are held to the pixel output of the C code.
    //! `test_display_text_draw()` is not here: it draws through the font code
    //! and is ported with pm-font.

    use super::*;
    use crate::mock::{MockDisplay, DISPLAY_BPP, DISPLAY_HEIGHT, DISPLAY_WIDTH};

    /// `BLACK` of the C tests, the `0` they filled with
    const BLACK: Color = Color::Black;
    /// `WHITE` of the C tests, the `1` they drew with
    const WHITE: Color = Color::White;

    /// Expected framebuffer of `test_display_rect_draw()` in
    /// `test/host/Platinenmacher/test_display/test.c`, one row of the display
    /// per line.
    #[rustfmt::skip]
    const RECT_DRAW: [u8; 400] = [
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    ];

    /// Expected framebuffer of `test_display_line_draw()` in
    /// `test/host/Platinenmacher/test_display/test.c`, one row of the display
    /// per line.
    #[rustfmt::skip]
    const LINE_DRAW: [u8; 400] = [
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,1,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,1,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,0,0,1,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,0,1,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,1,1,0,0,0,1,0,0,1,1,1,0,
        0,0,0,0,0,0,0,1,0,0,1,1,1,1,1,1,0,0,0,0,
        0,0,0,0,1,1,1,1,1,1,0,0,0,0,1,0,0,0,0,0,
        1,1,1,1,0,1,0,0,0,0,0,0,0,0,0,1,0,0,0,0,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
    ];

    /// Expected framebuffer of `test_display_circle_draw()` in
    /// `test/host/Platinenmacher/test_display/test.c`, one row of the display
    /// per line.
    #[rustfmt::skip]
    const CIRCLE_DRAW: [u8; 400] = [
        0,0,0,0,0,1,0,0,0,0,0,0,1,1,1,1,1,1,1,0,
        0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0,0,1,
        0,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,1,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,1,1,0,0,0,1,0,0,0,1,1,0,0,0,0,
        1,1,1,1,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,
        0,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,1,0,0,0,
        0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,0,0,1,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,0,0,0,1,0,0,0,0,0,1,0,1,
        0,0,0,1,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,0,0,0,0,1,1,0,0,0,0,0,0,0,1,1,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    ];

    /// Expected framebuffer of `test_display_circle_draw_segment()` in
    /// `test/host/Platinenmacher/test_display/test.c`, one row of the display
    /// per line.
    #[rustfmt::skip]
    const CIRCLE_DRAW_SEGMENT: [u8; 400] = [
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,0,
        0,0,0,1,0,0,0,1,1,1,1,0,0,0,0,0,0,1,0,0,
        0,0,1,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
        0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,
        0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,
        0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,0,0,1,0,
        0,0,0,1,0,0,0,0,0,0,1,1,1,1,0,0,0,1,0,0,
        0,0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,
    ];

    /// Expected framebuffer of `test_display_rect_fill()` in
    /// `test/host/Platinenmacher/test_display/test.c`, one row of the display
    /// per line.
    #[rustfmt::skip]
    const RECT_FILL: [u8; 400] = [
        0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,1,1,1,1,1,0,0,0,0,0,
        0,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,1,1,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,0,0,0,0,0,
    ];

    /// Expected framebuffer of `test_display_circle_fill()` in
    /// `test/host/Platinenmacher/test_display/test.c`, one row of the display
    /// per line.
    #[rustfmt::skip]
    const CIRCLE_FILL: [u8; 400] = [
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
        0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
        0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,
        0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
        0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,0,0,0,
        0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,0,
        0,0,0,0,0,0,0,1,1,1,1,1,1,1,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    ];

    /// `setUp()` of the C test: a 20x20, 8 bpp, unrotated display on the mock.
    fn set_up() -> Display<MockDisplay> {
        MockDisplay::display()
    }

    /// `printf_fb()` of the C test helper, used for failure messages.
    fn render(fb: &[u8]) -> alloc::string::String {
        use core::fmt::Write;
        let mut out = alloc::string::String::new();
        for y in 0..DISPLAY_HEIGHT as usize {
            for x in 0..DISPLAY_WIDTH as usize {
                let _ = write!(out, "{}", fb[y * DISPLAY_WIDTH as usize + x]);
            }
            out.push('\n');
        }
        out
    }

    fn assert_fb(dsp: &Display<MockDisplay>, expected: &[u8], message: &str) {
        assert_eq!(
            dsp.fb(),
            expected,
            "{message}\nexpected:\n{}\ngot:\n{}",
            render(expected),
            render(dsp.fb())
        );
    }

    #[test]
    fn test_display_init() {
        let dsp = set_up();
        assert_eq!(0, dsp.fb()[0], "framebuffer");
        assert_eq!(DISPLAY_HEIGHT, dsp.size().height, "height");
        assert_eq!(DISPLAY_WIDTH, dsp.size().width, "width");
        assert_eq!(DISPLAY_BPP, dsp.bpp(), "bpp");
        assert_eq!(Rotation::Deg0, dsp.rotation(), "rotation");
        assert_eq!(
            DISPLAY_WIDTH as usize * DISPLAY_HEIGHT as usize,
            dsp.fb_size(),
            "framebuffer size"
        );
    }

    #[test]
    fn sizeof_fb_matches_the_c_inline() {
        assert_eq!(400, sizeof_fb(Rect::new(0, 0, 20, 20), 8));
        // the ACeP panel of the firmware: 4 bits per pixel
        assert_eq!(192_000, sizeof_fb(Rect::new(0, 0, 800, 480), 4));
    }

    #[test]
    fn test_display_draw_pixel() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.pixel_draw(0, 0, WHITE), "draw pixel 0,0 white");
        assert_eq!(WHITE.as_u8(), dsp.fb()[0], "0 Pixel is WHITE");
        // the C test set dsp->write_pixel = 0 here
        dsp.target_mut().write_pixel_enabled = false;
        assert_eq!(
            Err(Error::Fail),
            dsp.pixel_draw(0, 0, WHITE),
            "draw pixel without a working target"
        );
    }

    #[test]
    fn test_display_draw_out_of_bound() {
        let mut dsp = set_up();
        assert_eq!(
            Ok(()),
            dsp.pixel_draw(DISPLAY_HEIGHT as i16 - 1, DISPLAY_WIDTH as i16 - 1, WHITE),
            "pixel draw in bounds"
        );
        assert_eq!(
            Err(Error::OutOfBounds),
            dsp.pixel_draw(DISPLAY_HEIGHT as i16, DISPLAY_WIDTH as i16 - 1, WHITE),
            "pixel draw out of bounds x"
        );
        assert_eq!(
            Err(Error::OutOfBounds),
            dsp.pixel_draw(DISPLAY_HEIGHT as i16 - 1, DISPLAY_WIDTH as i16, WHITE),
            "pixel draw out of bounds y"
        );
        assert_eq!(
            Err(Error::OutOfBounds),
            dsp.pixel_draw(-1, 0, WHITE),
            "pixel draw out of bounds negative x"
        );
        assert_eq!(
            Err(Error::OutOfBounds),
            dsp.pixel_draw(0, -1, WHITE),
            "pixel draw out of bounds negative y"
        );
    }

    #[test]
    fn test_display_fill() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(WHITE), "did fill");
        assert!(
            dsp.fb().iter().all(|&p| p == WHITE.as_u8()),
            "pixels are white"
        );
    }

    #[test]
    fn test_display_draw_colors() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.pixel_draw(0, 0, WHITE), "draw pixel 0,0 white");
        assert_eq!(Ok(()), dsp.pixel_draw(1, 0, BLACK), "draw pixel 1,0 black");
        assert_eq!(
            Ok(()),
            dsp.pixel_draw(1, 0, Color::Transparent),
            "draw pixel 1,0 transparent"
        );
        assert_eq!(WHITE.as_u8(), dsp.fb()[0], "0 Pixel is WHITE");
        assert_eq!(
            BLACK.as_u8(),
            dsp.fb()[1],
            "1 Pixel is still BLACK, transparent does not write"
        );
    }

    #[test]
    fn test_display_rect_draw() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.rect_draw(1, 1, 4, 4, WHITE));
        assert_fb(&dsp, &RECT_DRAW, "square not as expected");
    }

    #[test]
    fn test_display_line_draw() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.line_draw(1, 1, 5, 3, WHITE));
        assert_eq!(Ok(()), dsp.line_draw(3, 5, 1, 1, WHITE));
        assert_eq!(Ok(()), dsp.line_draw(10, 6, 15, 13, WHITE));
        assert_eq!(Ok(()), dsp.line_draw(2, 16, 16, 3, WHITE));
        assert_eq!(Ok(()), dsp.line_draw(3, 16, 2, 16, WHITE));
        assert_eq!(Ok(()), dsp.line_draw(0, 13, 18, 10, WHITE));
        assert_eq!(
            Err(Error::OutOfBounds),
            dsp.line_draw(
                16,
                16,
                DISPLAY_HEIGHT as i16 + 1,
                DISPLAY_WIDTH as i16 + 1,
                WHITE
            ),
            "line with an endpoint outside the display"
        );
        assert_fb(&dsp, &LINE_DRAW, "line not as expected");
    }

    #[test]
    fn test_display_circle_draw() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.circle_draw(1, 1, 5, WHITE));
        assert_eq!(Ok(()), dsp.circle_draw(10, 10, 8, WHITE));
        assert_eq!(Ok(()), dsp.circle_draw(15, 5, 6, WHITE));
        assert_fb(&dsp, &CIRCLE_DRAW, "circle not as expected");
    }

    #[test]
    fn test_display_circle_draw_segment() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.circle_draw_segment(10, 10, 10, WHITE, 0x01));
        assert_eq!(Ok(()), dsp.circle_draw_segment(10, 10, 8, WHITE, 0x02));
        assert_eq!(Ok(()), dsp.circle_draw_segment(10, 10, 10, WHITE, 0x04));
        assert_eq!(Ok(()), dsp.circle_draw_segment(10, 10, 8, WHITE, 0x08));
        assert_eq!(Ok(()), dsp.circle_draw_segment(10, 10, 10, WHITE, 0x10));
        assert_eq!(Ok(()), dsp.circle_draw_segment(10, 10, 8, WHITE, 0x20));
        assert_eq!(Ok(()), dsp.circle_draw_segment(10, 10, 10, WHITE, 0x40));
        assert_eq!(Ok(()), dsp.circle_draw_segment(10, 10, 8, WHITE, 0x80));
        assert_fb(
            &dsp,
            &CIRCLE_DRAW_SEGMENT,
            "circle segments not as expected",
        );
    }

    #[test]
    fn test_display_rect_fill() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.rect_fill(1, 1, 5, 5, WHITE));
        assert_eq!(Ok(()), dsp.rect_fill(7, 12, 8, 8, WHITE));
        assert_eq!(Ok(()), dsp.rect_fill(18, 8, 5, 5, WHITE));
        assert_eq!(Ok(()), dsp.rect_fill(10, 0, 5, 5, WHITE));
        assert_fb(&dsp, &RECT_FILL, "filled rect not as expected");
    }

    #[test]
    #[ignore = "this fails because the current circle code creates holes! (TEST_IGNORE_MESSAGE of the C test)"]
    fn test_display_circle_fill() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.circle_fill(10, 10, 8, WHITE));
        assert_fb(&dsp, &CIRCLE_FILL, "filled circle not as expected");
    }

    /// The holes of `display_circle_fill()` are the reason the C test above is
    /// ignored; the outline pixels it does draw have to be there.
    #[test]
    fn circle_fill_draws_the_center_and_every_ring() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.circle_fill(10, 10, 8, WHITE));
        let pixel = |x: usize, y: usize| dsp.fb()[y * DISPLAY_WIDTH as usize + x];
        assert_eq!(WHITE.as_u8(), pixel(10, 10), "center");
        assert_eq!(WHITE.as_u8(), pixel(10, 3), "top of the outer ring");
        assert_eq!(WHITE.as_u8(), pixel(3, 10), "left of the outer ring");
        assert_eq!(BLACK.as_u8(), pixel(1, 1), "outside stays untouched");
    }

    /// 1 bit per pixel bitmap, one byte per column: `draw_raw()` has to lay the
    /// bits out along y, `Rotation::Deg90` transposes that.
    #[test]
    fn draw_raw_lays_bytes_out_as_columns() {
        // column 0: pixel 0; column 1: pixels 0 and 1; column 7: pixel 7
        let img: [u8; 8] = [0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80];
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.draw_raw(&img, 0, 0, 8, 8, WHITE, BLACK));

        let pixel = |fb: &[u8], x: usize, y: usize| fb[y * DISPLAY_WIDTH as usize + x];
        assert_eq!(WHITE.as_u8(), pixel(dsp.fb(), 0, 0));
        assert_eq!(WHITE.as_u8(), pixel(dsp.fb(), 1, 0));
        assert_eq!(WHITE.as_u8(), pixel(dsp.fb(), 1, 1));
        assert_eq!(BLACK.as_u8(), pixel(dsp.fb(), 0, 1));
        assert_eq!(WHITE.as_u8(), pixel(dsp.fb(), 7, 7));
        assert_eq!(
            4,
            dsp.fb().iter().filter(|&&p| p == WHITE.as_u8()).count(),
            "the four set bits of the bitmap and no other pixel"
        );

        let unrotated = alloc::vec::Vec::from(dsp.fb());
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(
            Ok(()),
            dsp.draw_raw_rot(&img, 0, 0, 8, 8, WHITE, BLACK, Rotation::Deg90)
        );
        for y in 0..8 {
            for x in 0..8 {
                assert_eq!(
                    pixel(&unrotated, y, x),
                    pixel(dsp.fb(), x, y),
                    "rotated by 90 degrees the bitmap is the transpose at {x},{y}"
                );
            }
        }
    }

    #[test]
    fn draw_raw_reports_a_short_bitmap_instead_of_reading_past_it() {
        let mut dsp = set_up();
        assert_eq!(
            Err(Error::OutOfBounds),
            dsp.draw_raw(&[0x01], 0, 0, 8, 8, WHITE, BLACK)
        );
    }

    /// `image_data` of the C test, expanded through the mock's `decompress()`:
    /// `data[x * size->width + y]`.
    #[test]
    fn draw_image_expands_through_decompress() {
        let image_data: [u8; 4] = [0, 0, 1, 1];
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(Ok(()), dsp.draw_image(&image_data, 1, 1, 2, 2));

        let pixel = |x: usize, y: usize| dsp.fb()[y * DISPLAY_WIDTH as usize + x];
        assert_eq!(BLACK.as_u8(), pixel(1, 1), "data[0]");
        assert_eq!(BLACK.as_u8(), pixel(1, 2), "data[1]");
        assert_eq!(WHITE.as_u8(), pixel(2, 1), "data[2]");
        assert_eq!(WHITE.as_u8(), pixel(2, 2), "data[3]");
        assert_eq!(
            2,
            dsp.fb().iter().filter(|&&p| p == WHITE.as_u8()).count(),
            "the image is 2x2 pixels"
        );
    }

    #[test]
    fn draw_image_outside_the_display_is_out_of_bounds() {
        let image_data: [u8; 4] = [1, 1, 1, 1];
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(
            Err(Error::OutOfBounds),
            dsp.draw_image(&image_data, DISPLAY_WIDTH as i16, 1, 2, 2),
            "image starting right of the display"
        );
        assert!(
            dsp.fb().iter().all(|&p| p == BLACK.as_u8()),
            "every pixel of it was clipped"
        );
        assert_eq!(
            Err(Error::OutOfBounds),
            dsp.draw_image(&image_data, -1, 1, 2, 2),
            "image starting left of the display"
        );
        assert_eq!(
            2,
            dsp.fb().iter().filter(|&&p| p == WHITE.as_u8()).count(),
            "only its second column reaches column 0 of the display"
        );
    }

    /// The image is drawn even though the C code reports the corner as out of
    /// bounds; the pixels outside are clipped by `pixel_draw()`.
    #[test]
    fn draw_image_partly_outside_still_draws_the_visible_pixels() {
        let image_data: [u8; 4] = [1, 1, 1, 1];
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.fill(BLACK));
        assert_eq!(
            Ok(()),
            dsp.draw_image(&image_data, DISPLAY_WIDTH as i16 - 1, 1, 2, 2)
        );
        assert_eq!(
            2,
            dsp.fb().iter().filter(|&&p| p == WHITE.as_u8()).count(),
            "only the column inside the display was drawn"
        );
    }

    #[test]
    fn commit_fb_updates_the_panel() {
        let mut dsp = set_up();
        assert_eq!(Ok(()), dsp.commit_fb());
        assert_eq!(Ok(()), dsp.commit_fb());
        assert_eq!(2, dsp.target().updates);
    }

    /// Rotation is state of the display that the panel driver reads; the mock
    /// ignores it, but it has to reach the target unchanged.
    #[test]
    fn rotation_reaches_the_target() {
        #[derive(Default)]
        struct RotationSpy {
            seen: Option<Rotation>,
        }
        impl DisplayTarget for RotationSpy {
            fn write_pixel(
                &mut self,
                fb: FrameBuffer<'_>,
                x: i16,
                y: i16,
                color: Color,
            ) -> Result<()> {
                self.seen = Some(fb.rotation);
                assert_eq!(8, fb.bpp);
                assert_eq!(DISPLAY_WIDTH, fb.size.width);
                fb.data[y as usize * fb.size.width as usize + x as usize] = color.as_u8();
                Ok(())
            }
            fn decompress(&self, _size: &Rect, _x: i16, _y: i16, _data: &[u8]) -> Color {
                Color::Black
            }
            fn update(&mut self, _fb: &[u8]) -> Result<()> {
                Ok(())
            }
        }

        for rotation in [
            Rotation::Deg0,
            Rotation::Deg90,
            Rotation::Deg180,
            Rotation::Deg270,
        ] {
            let mut dsp = Display::new(
                DISPLAY_WIDTH,
                DISPLAY_HEIGHT,
                8,
                rotation,
                RotationSpy::default(),
            );
            assert_eq!(Ok(()), dsp.pixel_draw(2, 3, Color::White));
            assert_eq!(Some(rotation), dsp.target().seen);
            assert_eq!(rotation, dsp.rotation());
        }
    }
}
