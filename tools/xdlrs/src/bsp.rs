//! A box volume's BSP as the level tools make it: tools/bsp_build.py (prism, the triangles'
//! planes, the solid-leaf builder) and world_lib.bspn0_write, with numpy's float64 arithmetic
//! (sums left to right, no fused multiply-add) so the planes come out bit for bit the same.

use crate::error::{Error, Result};

pub type V3 = [f64; 3];
/// (plane a, b, c, d) as f32 values held in f64, vertices, and the node's children
pub type Plane = [f64; 4];

const EPS: f64 = 1e-4;

fn sub(a: V3, b: V3) -> V3 {
    [a[0] - b[0], a[1] - b[1], a[2] - b[2]]
}

/// np.cross
fn cross(a: V3, b: V3) -> V3 {
    [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]
}

fn dot(a: V3, b: V3) -> f64 {
    a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
}

/// Triangles of an extruded outline (points (x, z)): side walls, top, bottom, facing out.
pub fn prism(outline_xz: &[(f64, f64)], y0: f64, y1: f64) -> Vec<[V3; 3]> {
    let pts = outline_xz.to_vec();
    let n = pts.len();
    let mut tris: Vec<[V3; 3]> = vec![];
    let add = |tris: &mut Vec<[V3; 3]>, a: V3, b: V3, c: V3, want: V3| {
        let (b, c) = if dot(cross(sub(b, a), sub(c, a)), want) < 0.0 { (c, b) } else { (b, c) };
        tris.push([a, b, c]);
    };
    let area2 = |a: (f64, f64), b: (f64, f64), c: (f64, f64)| (b.0 - a.0) * (c.1 - a.1) - (b.1 - a.1) * (c.0 - a.0);
    let mut s = 0.0;
    for i in 0..n {
        s += area2((0.0, 0.0), pts[i], pts[(i + 1) % n]);
    }
    let turn = if s > 0.0 { 1.0 } else { -1.0 };
    for i in 0..n {
        let ((ax, az), (bx, bz)) = (pts[i], pts[(i + 1) % n]);
        let out = [(bz - az) * turn, 0.0, -(bx - ax) * turn];
        let (a0, b0, a1, b1) = ([ax, y0, az], [bx, y0, bz], [ax, y1, az], [bx, y1, bz]);
        add(&mut tris, a0, a1, b1, out);
        add(&mut tris, a0, b1, b0, out);
    }
    let cap = |tris: &mut Vec<[V3; 3]>, a: (f64, f64), b: (f64, f64), c: (f64, f64)| {
        add(tris, [a.0, y1, a.1], [b.0, y1, b.1], [c.0, y1, c.1], [0.0, 1.0, 0.0]);
        add(tris, [a.0, y0, a.1], [b.0, y0, b.1], [c.0, y0, c.1], [0.0, -1.0, 0.0]);
    };
    let mut idx: Vec<usize> = (0..n).collect();
    let mut guard = 0;
    while idx.len() > 3 && guard < n * n + 8 {
        guard += 1;
        let m = idx.len();
        for k in 0..m {
            let (i0, i1, i2) = (idx[(k + m - 1) % m], idx[k], idx[(k + 1) % m]);
            let (a, b, c) = (pts[i0], pts[i1], pts[i2]);
            if area2(a, b, c) * turn <= 0.0 {
                continue;
            }
            if idx.iter().any(|&j| {
                j != i0 && j != i1 && j != i2 && area2(a, b, pts[j]) * turn >= 0.0 && area2(b, c, pts[j]) * turn >= 0.0 && area2(c, a, pts[j]) * turn >= 0.0
            }) {
                continue;
            }
            cap(&mut tris, a, b, c);
            idx.remove(k);
            break;
        }
    }
    let p: Vec<(f64, f64)> = idx.iter().map(|&i| pts[i]).collect();
    cap(&mut tris, p[0], p[1], p[2]);
    tris
}

/// The plane of a counter-clockwise polygon (Newell), normal towards the viewer.
fn plane_of(v: &[V3]) -> Option<Plane> {
    let mut n = [0.0f64; 3];
    for i in 0..v.len() {
        let c = cross(v[i], v[(i + 1) % v.len()]);
        n = [n[0] + c[0], n[1] + c[1], n[2] + c[2]];
    }
    let ln = dot(n, n).sqrt();
    if ln < 1e-12 {
        return None;
    }
    let n = [n[0] / ln, n[1] / ln, n[2] / ln];
    let k = v.len() as f64;
    let mut m = [0.0f64; 3];
    for (j, mj) in m.iter_mut().enumerate() {
        let mut s = v[0][j];
        for x in &v[1..] {
            s += x[j];
        }
        *mj = s / k;
    }
    // numpy's dot (BLAS) sums from +0.0: a sum of negative zeros is +0.0 there
    Some([n[0], n[1], n[2], -(0.0 + n[0] * m[0] + n[1] * m[1] + n[2] * m[2])])
}

fn f32_plane(p: Plane) -> Plane {
    p.map(|x| x as f32 as f64)
}

pub struct Poly {
    pub plane: Plane,
    pub verts: Vec<V3>,
}

/// Triangles (counter-clockwise from outside) -> builder polygons; thin ones dropped.
pub fn polys_from_triangles(tris: &[[V3; 3]]) -> Vec<Poly> {
    tris.iter().filter_map(|t| plane_of(t).map(|p| Poly { plane: f32_plane(p), verts: t.to_vec() })).collect()
}

enum Side {
    On,
    Front,
    Back,
    Span(Option<Vec<V3>>, Option<Vec<V3>>),
}

fn split_poly(verts: &[V3], plane: Plane, eps: f64) -> Side {
    let n = [plane[0], plane[1], plane[2]];
    let d: Vec<f64> = verts.iter().map(|v| dot(*v, n) + plane[3]).collect();
    let pos = d.iter().any(|&x| x > eps);
    let neg = d.iter().any(|&x| x < -eps);
    if !pos && !neg {
        return Side::On;
    }
    if !neg {
        return Side::Front;
    }
    if !pos {
        return Side::Back;
    }
    let (mut fr, mut bk) = (vec![], vec![]);
    let m = verts.len();
    for i in 0..m {
        let (a, b, da, db) = (verts[i], verts[(i + 1) % m], d[i], d[(i + 1) % m]);
        if da >= -eps {
            fr.push(a);
        }
        if da <= eps {
            bk.push(a);
        }
        if (da > eps && db < -eps) || (da < -eps && db > eps) {
            let t = da / (da - db);
            let x = [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t];
            fr.push(x);
            bk.push(x);
        }
    }
    Side::Span(if fr.len() >= 3 { Some(fr) } else { None }, if bk.len() >= 3 { Some(bk) } else { None })
}

/// One node: (plane, front child, back child), pre-order.
pub type Node = (Plane, Option<usize>, Option<usize>);

/// bsp_build.build (opposite="back"): the splitter is the first polygon; the others go in
/// front, behind, or are cut in two.
pub fn build(polys: Vec<Poly>, limit: usize) -> Result<Vec<Node>> {
    fn rec(items: Vec<Poly>, tree: &mut Vec<Option<Node>>, limit: usize) -> Result<usize> {
        let k = tree.len();
        if k >= limit {
            return Err(Error::uncaught(format!("more than {limit} nodes")));
        }
        let mut it = items.into_iter();
        let first = it.next().unwrap();
        let plane = first.plane;
        tree.push(None);
        let n = [plane[0], plane[1], plane[2]];
        let (mut front, mut back) = (vec![], vec![]);
        for p in it {
            match split_poly(&p.verts, plane, EPS) {
                Side::On => {
                    if dot(n, [p.plane[0], p.plane[1], p.plane[2]]) > 0.0 {
                        front.push(p);
                    } else {
                        back.push(p);
                    }
                }
                Side::Front => front.push(p),
                Side::Back => back.push(p),
                Side::Span(f, b) => {
                    if let Some(f) = f {
                        front.push(Poly { plane: p.plane, verts: f });
                    }
                    if let Some(b) = b {
                        back.push(Poly { plane: p.plane, verts: b });
                    }
                }
            }
        }
        let fi = if front.is_empty() { None } else { Some(rec(front, tree, limit)?) };
        let bi = if back.is_empty() { None } else { Some(rec(back, tree, limit)?) };
        tree[k] = Some((plane, fi, bi));
        Ok(k)
    }
    let mut tree = vec![];
    if !polys.is_empty() {
        rec(polys, &mut tree, limit)?;
    }
    Ok(tree.into_iter().map(|n| n.unwrap()).collect())
}

/// world_lib.bspn0_write: BSPN0 nodes (front child right after its node), unused bytes 0xCD.
pub fn bspn0_write(tree: &[Node], big: bool) -> Result<Vec<u8>> {
    let mut out = vec![];
    let f = |x: f64| if big { (x as f32).to_be_bytes() } else { (x as f32).to_le_bytes() };
    for (k, (plane, front, back)) in tree.iter().enumerate() {
        if front.is_some_and(|fr| fr != k + 1) {
            return Err(Error::uncaught("BSPN0 needs the front child right after its node"));
        }
        for x in plane {
            out.extend(f(*x));
        }
        out.push(front.is_some() as u8 | if back.is_some() { 2 } else { 0 });
        out.push(0xCD);
        match back {
            Some(b) => {
                let v = (b - k) as u16;
                out.extend(if big { v.to_be_bytes() } else { v.to_le_bytes() });
            }
            None => out.extend([0xCD, 0xCD]),
        }
        out.extend([0xCDu8; 0xC]);
    }
    Ok(out)
}

/// The BSP of an axis-aligned box lo..hi, as pack bytes (xdl_level's box volumes).
pub fn box_bsp(lo: V3, hi: V3, big: bool) -> Result<Vec<u8>> {
    let tris = prism(&[(lo[0], lo[2]), (hi[0], lo[2]), (hi[0], hi[2]), (lo[0], hi[2])], lo[1], hi[1]);
    bspn0_write(&build(polys_from_triangles(&tris), 0xFFFF)?, big)
}
