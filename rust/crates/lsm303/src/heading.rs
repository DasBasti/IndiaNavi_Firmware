//! Heading and tilt computation for the north indicator.
//!
//! There is no counterpart to this module in `lib/lsm303/lsm303.c`: the C
//! driver only ever read the tap source register, and `north_indicator_label`
//! in the C GUI is a static icon with no sensor input. The formulas here are
//! the tilt-compensated e-compass of ST application note AN3192, which is the
//! reference algorithm for this sensor family.

use crate::float::{abs, asin, atan2, cos, sin, sqrt};

use crate::{Acceleration, MagneticField};

const RAD_TO_DEG: f32 = 180.0 / core::f32::consts::PI;

/// Attitude of the board, derived from the accelerometer.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct Tilt {
    /// Rotation about the sensor Y axis, in degrees, range -90..=90.
    pub pitch_deg: f32,
    /// Rotation about the sensor X axis, in degrees, range -90..=90.
    pub roll_deg: f32,
}

/// Normalise an angle in degrees into `[0, 360)`.
fn wrap_360(deg: f32) -> f32 {
    let mut d = deg % 360.0;
    if d < 0.0 {
        d += 360.0;
    }
    // `-0.0 % 360.0` is `-0.0`, which is `< 0.0 == false`; force a clean zero.
    if d == 0.0 {
        0.0
    } else {
        d
    }
}

/// Pitch and roll from an accelerometer sample.
///
/// Returns `None` when the sample has no usable magnitude (all axes zero),
/// which happens in free fall or when the accelerometer is powered down.
pub fn tilt(acc: &Acceleration) -> Option<Tilt> {
    let (x, y, z) = (acc.x as f32, acc.y as f32, acc.z as f32);
    let norm = sqrt(x * x + y * y + z * z);
    if norm == 0.0 {
        return None;
    }
    let (ax, ay) = (x / norm, y / norm);

    let pitch = asin((-ax).clamp(-1.0, 1.0));
    let cos_pitch = cos(pitch);
    // Straight up or straight down: roll is not observable, report 0.
    let roll = if abs(cos_pitch) < f32::EPSILON {
        0.0
    } else {
        asin((ay / cos_pitch).clamp(-1.0, 1.0))
    };

    Some(Tilt {
        pitch_deg: pitch * RAD_TO_DEG,
        roll_deg: roll * RAD_TO_DEG,
    })
}

/// Heading of a magnetometer sample assuming the board lies flat.
///
/// The returned angle is the direction of the horizontal field component,
/// measured in degrees from the sensor `+X` axis towards the sensor `+Y` axis,
/// normalised to `[0, 360)`. Mapping that onto a compass bearing depends on how
/// the sensor is oriented on the board, which is the caller's business.
///
/// Returns `None` for an all-zero sample, where no direction exists.
pub fn heading_degrees(mag: &MagneticField) -> Option<f32> {
    if mag.x == 0 && mag.y == 0 {
        return None;
    }
    Some(wrap_360(atan2(mag.y as f32, mag.x as f32) * RAD_TO_DEG))
}

/// Tilt-compensated heading, in the same convention as [`heading_degrees`].
///
/// Projects the magnetometer sample back into the horizontal plane using the
/// pitch and roll derived from `acc` (AN3192):
///
/// ```text
/// Xh = mx*cos(pitch) + mz*sin(pitch)
/// Yh = mx*sin(roll)*sin(pitch) + my*cos(roll) - mz*sin(roll)*cos(pitch)
/// heading = atan2(Yh, Xh)
/// ```
///
/// Returns `None` if the accelerometer sample is unusable or the projected
/// horizontal field vanishes (sensor pointing along the field lines).
pub fn tilt_compensated_heading_degrees(acc: &Acceleration, mag: &MagneticField) -> Option<f32> {
    let tilt = tilt(acc)?;
    let pitch = tilt.pitch_deg / RAD_TO_DEG;
    let roll = tilt.roll_deg / RAD_TO_DEG;
    let (sp, cp) = (sin(pitch), cos(pitch));
    let (sr, cr) = (sin(roll), cos(roll));

    let (mx, my, mz) = (mag.x as f32, mag.y as f32, mag.z as f32);
    let xh = mx * cp + mz * sp;
    let yh = mx * sr * sp + my * cr - mz * sr * cp;

    if xh == 0.0 && yh == 0.0 {
        return None;
    }
    Some(wrap_360(atan2(yh, xh) * RAD_TO_DEG))
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Rotate an earth-frame vector into the sensor frame for a pure pitch of
    /// `theta` degrees about the sensor Y axis.
    fn rotate_pitch(v: (f32, f32, f32), theta_deg: f32) -> (i16, i16, i16) {
        let t = theta_deg / RAD_TO_DEG;
        let (s, c) = (sin(t), cos(t));
        (
            (v.0 * c - v.2 * s) as i16,
            v.1 as i16,
            (v.0 * s + v.2 * c) as i16,
        )
    }

    /// Same, for a pure roll of `phi` degrees about the sensor X axis.
    fn rotate_roll(v: (f32, f32, f32), phi_deg: f32) -> (i16, i16, i16) {
        let p = phi_deg / RAD_TO_DEG;
        let (s, c) = (sin(p), cos(p));
        (
            v.0 as i16,
            (v.1 * c + v.2 * s) as i16,
            (-v.1 * s + v.2 * c) as i16,
        )
    }

    fn assert_close(actual: f32, expected: f32, tol: f32) {
        let diff = ((actual - expected + 540.0) % 360.0) - 180.0;
        assert!(
            abs(diff) <= tol,
            "expected {expected} +/- {tol}, got {actual}"
        );
    }

    #[test]
    fn flat_heading_follows_the_x_axis() {
        let north = MagneticField {
            x: 1000,
            y: 0,
            z: 0,
        };
        assert_close(heading_degrees(&north).unwrap(), 0.0, 0.01);
    }

    #[test]
    fn flat_heading_quadrants() {
        let cases = [
            ((1000, 0), 0.0),
            ((1000, 1000), 45.0),
            ((0, 1000), 90.0),
            ((-1000, 0), 180.0),
            ((0, -1000), 270.0),
            ((1000, -1000), 315.0),
        ];
        for ((x, y), expected) in cases {
            let m = MagneticField { x, y, z: 0 };
            assert_close(heading_degrees(&m).unwrap(), expected, 0.01);
        }
    }

    #[test]
    fn flat_heading_is_none_without_a_horizontal_component() {
        let m = MagneticField { x: 0, y: 0, z: 500 };
        assert_eq!(heading_degrees(&m), None);
    }

    #[test]
    fn tilt_recovers_pitch_and_roll() {
        // Level: reaction to gravity is +1 g on Z.
        let level = Acceleration {
            x: 0,
            y: 0,
            z: 1000,
        };
        let t = tilt(&level).unwrap();
        assert_close(t.pitch_deg, 0.0, 0.01);
        assert_close(t.roll_deg, 0.0, 0.01);

        // Pitched 30 degrees: ax = -sin(30) = -0.5.
        let pitched = Acceleration {
            x: -500,
            y: 0,
            z: 866,
        };
        let t = tilt(&pitched).unwrap();
        assert_close(t.pitch_deg, 30.0, 0.2);
        assert_close(t.roll_deg, 0.0, 0.2);

        // Rolled 45 degrees: ay = sin(45), az = cos(45).
        let rolled = Acceleration {
            x: 0,
            y: 707,
            z: 707,
        };
        let t = tilt(&rolled).unwrap();
        assert_close(t.pitch_deg, 0.0, 0.2);
        assert_close(t.roll_deg, 45.0, 0.2);
    }

    #[test]
    fn tilt_is_none_in_free_fall() {
        assert_eq!(tilt(&Acceleration { x: 0, y: 0, z: 0 }), None);
    }

    #[test]
    fn tilt_compensation_cancels_pitch() {
        // Earth frame: field 30 degrees off the X axis, with a vertical
        // component like the real geomagnetic field has.
        let field = (8660.0, 5000.0, -4000.0);
        let flat_heading = 30.0;

        for pitch in [-60.0, -30.0, 0.0, 15.0, 45.0] {
            let (mx, my, mz) = rotate_pitch(field, pitch);
            let (ax, ay, az) = rotate_pitch((0.0, 0.0, 1000.0), pitch);
            let heading = tilt_compensated_heading_degrees(
                &Acceleration {
                    x: ax,
                    y: ay,
                    z: az,
                },
                &MagneticField {
                    x: mx,
                    y: my,
                    z: mz,
                },
            )
            .unwrap();
            assert_close(heading, flat_heading, 1.0);
        }
    }

    #[test]
    fn tilt_compensation_cancels_roll() {
        let field = (8660.0, 5000.0, -4000.0);
        let flat_heading = 30.0;

        for roll in [-45.0, -20.0, 0.0, 20.0, 45.0] {
            let (mx, my, mz) = rotate_roll(field, roll);
            let (ax, ay, az) = rotate_roll((0.0, 0.0, 1000.0), roll);
            let heading = tilt_compensated_heading_degrees(
                &Acceleration {
                    x: ax,
                    y: ay,
                    z: az,
                },
                &MagneticField {
                    x: mx,
                    y: my,
                    z: mz,
                },
            )
            .unwrap();
            assert_close(heading, flat_heading, 1.0);
        }
    }

    #[test]
    fn tilt_compensated_heading_matches_flat_heading_when_level() {
        let level = Acceleration {
            x: 0,
            y: 0,
            z: 1000,
        };
        let m = MagneticField {
            x: 1200,
            y: -900,
            z: 3000,
        };
        let flat = heading_degrees(&m).unwrap();
        let compensated = tilt_compensated_heading_degrees(&level, &m).unwrap();
        assert_close(compensated, flat, 0.01);
    }

    #[test]
    fn tilt_compensated_heading_is_none_in_free_fall() {
        let m = MagneticField {
            x: 100,
            y: 100,
            z: 100,
        };
        assert_eq!(
            tilt_compensated_heading_degrees(&Acceleration { x: 0, y: 0, z: 0 }, &m),
            None
        );
    }

    #[test]
    fn wrap_360_normalises_negative_angles() {
        assert_eq!(wrap_360(0.0), 0.0);
        assert_eq!(wrap_360(-90.0), 270.0);
        assert_eq!(wrap_360(450.0), 90.0);
        assert_eq!(wrap_360(-360.0), 0.0);
    }
}
