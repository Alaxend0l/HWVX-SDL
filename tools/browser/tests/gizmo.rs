//! The level editor's axis gizmo (src/gizmo.rs): the picture's camera, rays and projections,
//! the axis under the pointer and a drag along it. No game needed.
//!
//!   cargo test --manifest-path tools/browser/Cargo.toml --test gizmo

use hwvx_browser::gizmo::{AxisDrag, View, add, along_axis, axis_at, gizmo_length};
use serde_json::json;

/// A camera at (0, 10, -100) looking along +z, the picture 400 x 300, 60 degrees high.
fn view() -> View {
    let ty = (30f32).to_radians().tan();
    View { pos: [0.0, 10.0, -100.0], dir: [0.0, 0.0, 1.0], right: [-1.0, 0.0, 0.0], up: [0.0, 1.0, 0.0], tan: [ty * 4.0 / 3.0, ty], size: [400.0, 300.0] }
}

fn near(a: [f32; 3], b: [f32; 3], e: f32) -> bool {
    (0..3).all(|i| (a[i] - b[i]).abs() < e)
}

#[test]
fn from_the_viewers_camera() {
    let c = json!({"pos": [0, 10, -100], "yaw": 0, "view": {"dir": [0, 0, 1], "right": [-1, 0, 0], "up": [0, 1, 0], "tan": [0.7698, 0.57735], "size": [400, 300]}});
    let v = View::from_camera(&c).unwrap();
    assert_eq!(v.size, [400.0, 300.0]);
    assert_eq!(v.right, [-1.0, 0.0, 0.0]);
    // an older game (no view), or a picture of no size: none
    assert!(View::from_camera(&json!({"pos": [0, 0, 0]})).is_none());
    let mut z = c.clone();
    z["view"]["size"] = json!([0, 0]);
    assert!(View::from_camera(&z).is_none());
}

#[test]
fn rays_and_projection() {
    let v = view();
    // the middle of the picture looks straight ahead
    let (o, d) = v.ray(200.0, 150.0);
    assert_eq!(o, v.pos);
    assert!(near(d, [0.0, 0.0, 1.0], 1e-6));
    // the picture's right is world +x here (the camera's right is the picture's left), its top +y
    let (_, d) = v.ray(400.0, 150.0);
    assert!(d[0] > 0.5 && d[1].abs() < 1e-6, "{d:?}");
    let (_, d) = v.ray(200.0, 0.0);
    assert!(d[1] > 0.4 && (d[1] / d[2] - (30f32).to_radians().tan()).abs() < 1e-5, "{d:?}");
    // a point a ray reaches goes back under its pixel
    for (x, y) in [(0.0, 0.0), (17.0, 280.0), (399.0, 3.0), (200.0, 150.0)] {
        let (o, d) = v.ray(x, y);
        let p = v.project(add(o, d, 57.0)).unwrap();
        assert!((p[0] - x).abs() < 1e-3 && (p[1] - y).abs() < 1e-3, "{p:?} for {x}, {y}");
    }
    // behind the camera: nowhere
    assert!(v.project([0.0, 10.0, -150.0]).is_none());
}

#[test]
fn the_axis_under_the_pointer() {
    let v = view();
    let at = [0.0, 0.0, 0.0];
    let len = 15.0;
    // x runs right of the centre, y up; z away from the camera (seen nearly end on: short)
    let c = v.project(at).unwrap();
    let xe = v.project(add(at, [1.0, 0.0, 0.0], len)).unwrap();
    let ye = v.project(add(at, [0.0, 1.0, 0.0], len)).unwrap();
    assert!(xe[0] > c[0] + 20.0 && (xe[1] - c[1]).abs() < 1e-3);
    assert!(ye[1] < c[1] - 20.0);
    let mid = |e: [f32; 2]| [(c[0] + e[0]) / 2.0, (c[1] + e[1]) / 2.0];
    assert_eq!(axis_at(&v, at, len, mid(xe), 6.0), Some(0));
    assert_eq!(axis_at(&v, at, len, mid(ye), 6.0), Some(1));
    // 5 pixels off the x axis: within 6, not within 4
    let off = [mid(xe)[0], mid(xe)[1] + 5.0];
    assert_eq!(axis_at(&v, at, len, off, 6.0), Some(0));
    assert_eq!(axis_at(&v, at, len, off, 4.0), None);
    // past the axis' end, or at the centre where all meet: none
    assert_eq!(axis_at(&v, at, len, [xe[0] + 20.0, xe[1]], 6.0), None);
    assert_eq!(axis_at(&v, at, len, c, 3.0), None);
    // z seen end on (straight down the camera's view): not taken; turned a little, it is
    let ze = v.project(add(at, [0.0, 0.0, 1.0], len)).unwrap();
    assert!(((ze[0] - c[0]).powi(2) + (ze[1] - c[1]).powi(2)).sqrt() < 4.0 + 6.0);
    let side = View { pos: [-60.0, 30.0, -60.0], dir: norm([60.0, -30.0, 60.0]), right: norm([-60.0, 0.0, 60.0]), up: [0.0, 0.0, 0.0], ..v };
    let side = View { up: cross(side.dir, side.right), ..side };
    let c = side.project(at).unwrap();
    let ze = side.project(add(at, [0.0, 0.0, 1.0], len)).unwrap();
    assert_eq!(axis_at(&side, at, len, [(c[0] + ze[0]) / 2.0, (c[1] + ze[1]) / 2.0], 4.0), Some(2));
}

fn norm(a: [f32; 3]) -> [f32; 3] {
    let l = (a[0] * a[0] + a[1] * a[1] + a[2] * a[2]).sqrt();
    [a[0] / l, a[1] / l, a[2] / l]
}

fn cross(a: [f32; 3], b: [f32; 3]) -> [f32; 3] {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}

#[test]
fn dragging_along_an_axis() {
    let v = view();
    let from = [5.0, 0.0, 0.0];
    // the nearest point of the x line to the ray through a pixel is under that pixel
    for x in [100.0f32, 250.0, 330.0] {
        let p = v.project([x / 10.0, 0.0, 0.0]).unwrap();
        let (o, d) = v.ray(p[0], p[1]);
        let s = along_axis(from, 0, o, d).unwrap();
        assert!((from[0] + s - x / 10.0).abs() < 1e-3, "{s} for {x}");
    }
    // a drag keeps the grab point under the pointer: pressed 3 units along x from the centre,
    // dragged to where 12 shows, the selection goes 9 along x, and only along x
    let press = v.project([8.0, 0.0, 0.0]).unwrap();
    let (o, d) = v.ray(press[0], press[1]);
    let drag = AxisDrag::start(0, from, Some(1.5), o, d).unwrap();
    let to = v.project([17.0, 0.0, 0.0]).unwrap();
    let (o, d) = v.ray(to[0], to[1]);
    let p = drag.place(o, d, 5000.0).unwrap();
    assert!(near(p, [14.0, 0.0, 0.0], 1e-3), "{p:?}");
    assert_eq!(drag.rot, Some(1.5));
    // off the axis (30 pixels below it): still only along x, near where the pointer is across
    let (o, d) = v.ray(to[0], to[1] + 30.0);
    let p = drag.place(o, d, 5000.0).unwrap();
    assert!(p[1] == 0.0 && p[2] == 0.0 && (p[0] - 14.0).abs() < 3.0, "{p:?}");
    // y: pressed 3 up the axis, dragged to 10 up: 7 up
    let press = v.project([5.0, 3.0, 0.0]).unwrap();
    let (o, d) = v.ray(press[0], press[1]);
    let up = AxisDrag::start(1, from, None, o, d).unwrap();
    let hi = v.project([5.0, 10.0, 0.0]).unwrap();
    let (o, d) = v.ray(hi[0], hi[1]);
    assert!(near(up.place(o, d, 5000.0).unwrap(), [5.0, 7.0, 0.0], 1e-3));
    // a ray along the axis: no drag; a move far out is held to `max`
    assert!(along_axis(from, 2, v.pos, [0.0, 0.0, 1.0]).is_none());
    let far = v.ray(399.0, 150.0);
    let p = drag.place(far.0, far.1, 2.0).unwrap();
    assert!((p[0] - from[0]).abs() <= 2.0 + 1e-4);
}

#[test]
fn gizmo_size_by_distance() {
    assert_eq!(gizmo_length(0.0), 15.0);
    assert_eq!(gizmo_length(f32::NAN), 15.0);
    assert_eq!(gizmo_length(60.0), 15.0);
    assert_eq!(gizmo_length(250.0), 30.0);
    assert_eq!(gizmo_length(1000.0), 120.0);
    assert_eq!(gizmo_length(1e7), 960.0);
    // about the same size in the picture: length over distance within a factor of 1.5 of 0.12
    for d in [130.0f32, 300.0, 700.0, 2000.0, 7000.0] {
        let r = gizmo_length(d) / d;
        assert!(r > 0.12 / 1.5 && r < 0.12 * 1.5, "{d}: {r}");
    }
}
