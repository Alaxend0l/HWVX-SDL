//! Crafted tiny inputs that claim huge sizes: the readers must refuse them before any big
//! allocation. A counting allocator records the largest single request on this thread.

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;

struct Track;

thread_local! {
    static BIGGEST: Cell<usize> = const { Cell::new(0) };
}

unsafe impl GlobalAlloc for Track {
    unsafe fn alloc(&self, l: Layout) -> *mut u8 {
        let _ = BIGGEST.try_with(|b| b.set(b.get().max(l.size())));
        unsafe { System.alloc(l) }
    }
    unsafe fn dealloc(&self, p: *mut u8, l: Layout) {
        unsafe { System.dealloc(p, l) }
    }
    unsafe fn realloc(&self, p: *mut u8, l: Layout, n: usize) -> *mut u8 {
        let _ = BIGGEST.try_with(|b| b.set(b.get().max(n)));
        unsafe { System.realloc(p, l, n) }
    }
}

#[global_allocator]
static A: Track = Track;

/// the largest single allocation f makes on this thread
fn biggest<T>(f: impl FnOnce() -> T) -> (T, usize) {
    BIGGEST.with(|b| b.set(0));
    let r = f();
    (r, BIGGEST.with(|b| b.get()))
}

const MB: usize = 1 << 20;

fn chunk(out: &mut Vec<u8>, ty: &[u8], data: &[u8]) {
    out.extend_from_slice(&(data.len() as u32).to_be_bytes());
    out.extend_from_slice(ty);
    out.extend_from_slice(data);
    out.extend_from_slice(&[0; 4]); // CRC: never reached
}

#[test]
fn huge_png_refused_before_decoding() {
    // a 1-bit grey 16384 x 16384 PNG header: 268 MP from a few dozen bytes
    let mut png = b"\x89PNG\r\n\x1a\n".to_vec();
    let mut ihdr = vec![];
    ihdr.extend_from_slice(&16384u32.to_be_bytes());
    ihdr.extend_from_slice(&16384u32.to_be_bytes());
    ihdr.extend_from_slice(&[1, 0, 0, 0, 0]);
    chunk(&mut png, b"IHDR", &ihdr);
    chunk(&mut png, b"IDAT", &[0x78, 0x9c, 0x03, 0x00]);
    chunk(&mut png, b"IEND", &[]);
    let (r, big) = biggest(|| xdlrs::modcheck::image::read_png(&png).map(|_| ()));
    let e = r.unwrap_err();
    assert!(e.contains("too large"), "{e}");
    assert!(big < MB, "allocated {big} bytes");
}

fn gltf(count: i64, stride: i64, offset: i64) -> String {
    // one buffer of 12 bytes (1.0, 2.0, 3.0) as a data URI
    format!(
        r#"{{"buffers":[{{"uri":"data:application/octet-stream;base64,AACAPwAAAEAAAEBA","byteLength":12}}],
        "bufferViews":[{{"buffer":0,"byteOffset":0,"byteStride":{stride}}}],
        "accessors":[{{"bufferView":0,"byteOffset":{offset},"componentType":5126,"type":"VEC3","count":{count}}}],
        "meshes":[{{"primitives":[{{"attributes":{{"POSITION":0}}}}]}}]}}"#
    )
}

#[test]
fn gltf_accessor_past_buffer_refused() {
    use std::path::Path;
    let p = Path::new("m.gltf");
    let run = |c: i64, s: i64, o: i64| xdlrs::modcheck::gltf_positions(p, gltf(c, s, o).as_bytes(), Path::new("."));
    // tight (0 and 12) and in range
    assert_eq!(run(1, 0, 0).unwrap(), vec![[1.0, 2.0, 3.0]]);
    assert_eq!(run(1, 12, 0).unwrap(), vec![[1.0, 2.0, 3.0]]);
    assert_eq!(run(0, 12, 12).unwrap(), Vec::<[f32; 3]>::new());
    // byteStride 0 once repeated the 12 bytes `count` times: 3 GB
    let (r, big) = biggest(|| run(250_000_000, 0, 0));
    assert!(r.unwrap_err().contains("past its 12-byte buffer"));
    assert!(big < MB, "allocated {big} bytes");
    let (r, big) = biggest(|| run(250_000_000, 4, 0));
    assert!(r.unwrap_err().contains("under 12"));
    assert!(big < MB, "allocated {big} bytes");
    assert!(run(1, 12, 4).unwrap_err().contains("past"));
    assert!(run(1, 12, -4).unwrap_err().contains("negative"));
    assert!(run(-1, 12, 0).unwrap_err().contains("negative count"));
    assert!(run(i64::MAX, 16, 0).unwrap_err().contains("past"));
    assert!(run(1, 12, i64::MAX).unwrap_err().contains("past"));
}
