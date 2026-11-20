//! The level editor's axis gizmo: the camera as the viewer reports it (`status`/`input`'s
//! camera.view: its basis and the picture's extents, src/port/viewer.c WriteCamera), points
//! projected to the picture and pointer rays out of it, as the game's own ScreenRay makes them,
//! and the axis under the pointer and where along it a drag goes.

use serde_json::Value;

/// The camera of the picture: where it is, its basis, the tangents of half its view and the
/// picture's size in pixels.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct View {
    pub pos: [f32; 3],
    pub dir: [f32; 3],
    /// the camera's right is the picture's left (Gfx_SetCameraMatrix)
    pub right: [f32; 3],
    pub up: [f32; 3],
    pub tan: [f32; 2],
    pub size: [f32; 2],
}

fn v3(v: &Value) -> Option<[f32; 3]> {
    let a = v.as_array()?;
    if a.len() < 3 {
        return None;
    }
    Some([a[0].as_f64()? as f32, a[1].as_f64()? as f32, a[2].as_f64()? as f32])
}

fn v2(v: &Value) -> Option<[f32; 2]> {
    let a = v.as_array()?;
    if a.len() < 2 {
        return None;
    }
    Some([a[0].as_f64()? as f32, a[1].as_f64()? as f32])
}

pub fn dot(a: [f32; 3], b: [f32; 3]) -> f32 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

pub fn sub(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

pub fn add(a: [f32; 3], b: [f32; 3], k: f32) -> [f32; 3] {
    [a[0] + b[0] * k, a[1] + b[1] * k, a[2] + b[2] * k]
}

/// The unit vector of axis k (0 x, 1 y, 2 z).
pub fn axis(k: usize) -> [f32; 3] {
    let mut a = [0.0; 3];
    a[k] = 1.0;
    a
}

impl View {
    /// From the viewer's camera object (its `pos` and `view`); None from an older game.
    pub fn from_camera(c: &Value) -> Option<View> {
        let v = &c["view"];
        let view = View { pos: v3(&c["pos"])?, dir: v3(&v["dir"])?, right: v3(&v["right"])?, up: v3(&v["up"])?, tan: v2(&v["tan"])?, size: v2(&v["size"])? };
        (view.tan[0] > 0.0 && view.tan[1] > 0.0 && view.size[0] > 0.0 && view.size[1] > 0.0).then_some(view)
    }

    /// The ray through picture pixel x, y: (origin, unit direction), as the game's ScreenRay.
    pub fn ray(&self, x: f32, y: f32) -> ([f32; 3], [f32; 3]) {
        let nx = (2.0 * x / self.size[0] - 1.0) * self.tan[0];
        let ny = (1.0 - 2.0 * y / self.size[1]) * self.tan[1];
        let mut d = [0.0; 3];
        for (i, di) in d.iter_mut().enumerate() {
            *di = self.dir[i] - self.right[i] * nx + self.up[i] * ny;
        }
        let l = dot(d, d).sqrt();
        (self.pos, [d[0] / l, d[1] / l, d[2] / l])
    }

    /// Where point p shows in the picture (pixels); None behind the camera (or nearly at it).
    pub fn project(&self, p: [f32; 3]) -> Option<[f32; 2]> {
        let v = sub(p, self.pos);
        let z = dot(v, self.dir);
        if z < 0.01 {
            return None;
        }
        let nx = -dot(v, self.right) / z;
        let ny = dot(v, self.up) / z;
        Some([(nx / self.tan[0] + 1.0) * 0.5 * self.size[0], (1.0 - ny / self.tan[1]) * 0.5 * self.size[1]])
    }
}

/// How long the gizmo's axes are for a camera `dist` away: about the same size in the picture
/// at any distance, in steps (so the marks aren't sent again for every small camera move).
pub fn gizmo_length(dist: f32) -> f32 {
    if !dist.is_finite() || dist <= 0.0 {
        return 15.0;
    }
    let steps = (dist / 125.0).log2().round().clamp(0.0, 6.0);
    15.0 * 2f32.powf(steps)
}

/// Distance of point q from the segment a..b (picture pixels).
fn seg_dist(q: [f32; 2], a: [f32; 2], b: [f32; 2]) -> f32 {
    let ab = [b[0] - a[0], b[1] - a[1]];
    let l2 = ab[0] * ab[0] + ab[1] * ab[1];
    let t = if l2 > 0.0 { (((q[0] - a[0]) * ab[0] + (q[1] - a[1]) * ab[1]) / l2).clamp(0.0, 1.0) } else { 0.0 };
    let c = [a[0] + ab[0] * t, a[1] + ab[1] * t];
    ((q[0] - c[0]).powi(2) + (q[1] - c[1]).powi(2)).sqrt()
}

/// The gizmo's axis (0 x, 1 y, 2 z) drawn under picture pixel q, within `tol` pixels: the
/// nearest. An axis seen end on (shorter than 4 pixels in the picture) can't be taken; nor can
/// the gizmo's centre alone (the first 15% of each axis: it is where all three meet).
pub fn axis_at(view: &View, pos: [f32; 3], len: f32, q: [f32; 2], tol: f32) -> Option<usize> {
    let a = view.project(pos)?;
    let mut best: Option<(f32, usize)> = None;
    for k in 0..3 {
        let Some(b) = view.project(add(pos, axis(k), len)) else { continue };
        let ab = [b[0] - a[0], b[1] - a[1]];
        if (ab[0] * ab[0] + ab[1] * ab[1]).sqrt() < 4.0 {
            continue;
        }
        let from = [a[0] + ab[0] * 0.15, a[1] + ab[1] * 0.15];
        let d = seg_dist(q, from, b);
        if d <= tol && best.map(|(bd, _)| d < bd).unwrap_or(true) {
            best = Some((d, k));
        }
    }
    best.map(|(_, k)| k)
}

/// Where along the line pos + s * axis(k) the ray (o, d) passes nearest: s. None when the ray
/// runs (nearly) along the axis, or the nearest point is behind the camera.
pub fn along_axis(pos: [f32; 3], k: usize, o: [f32; 3], d: [f32; 3]) -> Option<f32> {
    let u = axis(k);
    let w = sub(pos, o);
    let b = dot(u, d);
    let den = 1.0 - b * b;
    if den < 1e-4 {
        return None;
    }
    let (du, dd) = (dot(u, w), dot(d, w));
    let s = (b * dd - du) / den;
    let t = (dd - b * du) / den;
    (t > 0.0).then_some(s)
}

/// A drag along an axis: where the selection was and where along the axis the pointer took it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct AxisDrag {
    pub axis: usize,
    pub from: [f32; 3],
    pub s0: f32,
    pub rot: Option<f64>,
}

impl AxisDrag {
    /// Starts at the pointer's ray (None if the axis can't be dragged from this view).
    pub fn start(axis: usize, from: [f32; 3], rot: Option<f64>, o: [f32; 3], d: [f32; 3]) -> Option<AxisDrag> {
        Some(AxisDrag { axis, from, s0: along_axis(from, axis, o, d)?, rot })
    }

    /// The selection's place for the pointer's ray now (None: keep it where it is). A move is
    /// at most `max` units from where the drag started (a ray almost along the axis goes far).
    pub fn place(&self, o: [f32; 3], d: [f32; 3], max: f32) -> Option<[f32; 3]> {
        let s = along_axis(self.from, self.axis, o, d)?;
        let ds = (s - self.s0).clamp(-max, max);
        Some(add(self.from, axis(self.axis), ds))
    }
}
