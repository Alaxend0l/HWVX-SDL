//! FFmpeg, loaded at run time, for the PC's Bink movies and the PS2's MPEG-2 (.PSS) video: the
//! editor's copy of what the game does in src/port/bink.c, with the same rules.
//!
//! libavformat, libavcodec and libavutil are opened with libloading, never linked, so the build
//! needs neither them nor their headers; without them a Bink or PSS movie says "FFmpeg not found"
//! and THP movies (decoded here, src/movie.rs) still play. Releases 6.0 to 9.x (libavformat /
//! libavcodec majors 60-63, libavutil 58-61), checked before anything else is called. Everything
//! goes through functions except five structs' leading fields, the same in all of these releases
//! (bink.c checked them against their headers): AVFormatContext up to `streams`, AVStream up to
//! `avg_frame_rate`, AVPacket up to `flags`, AVFrame up to `format`. Decoder settings are
//! read through AVOptions. No libswscale or libswresample: 4:2:0 to RGBA is done here.
//!
//! Where: HWVX_FFMPEG_DIR, if set, is the only folder looked in. Otherwise the program's own
//! folder, the packages' (an .app's ../Frameworks, an AppImage's ../lib), then the system's search
//! (and Homebrew's folders on macOS), by each release's file names.

use libloading::Library;
use std::ffi::{CString, c_char, c_int, c_uint, c_void};
use std::path::{Path, PathBuf};
use std::sync::OnceLock;

const MIN_MAJOR: u32 = 60; // FFmpeg 6.0
const MAX_MAJOR: u32 = 63; // FFmpeg 9.x
const MEDIA_VIDEO: c_int = 0;
const MEDIA_AUDIO: c_int = 1;
const LOG_ERROR: c_int = 16;
const NOPTS: i64 = i64::MIN;
const PKT_KEY: c_int = 1;
const SEEK_BACKWARD: c_int = 1;

#[cfg(target_os = "macos")]
const EAGAIN: c_int = 35;
#[cfg(not(target_os = "macos"))]
const EAGAIN: c_int = 11;

#[repr(C)]
struct FormatHead {
    av_class: *const c_void,
    iformat: *const c_void,
    oformat: *const c_void,
    priv_data: *mut c_void,
    pb: *mut c_void,
    ctx_flags: c_int,
    nb_streams: c_uint,
    streams: *mut *mut StreamHead,
}

#[repr(C)]
struct StreamHead {
    av_class: *const c_void,
    index: c_int,
    id: c_int,
    codecpar: *mut c_void,
    priv_data: *mut c_void,
    tb_num: c_int,
    tb_den: c_int,
    start_time: i64,
    duration: i64,
    nb_frames: i64,
    disposition: c_int,
    discard: c_int,
    sar_num: c_int,
    sar_den: c_int,
    metadata: *mut c_void,
    avg_num: c_int,
    avg_den: c_int,
}

#[repr(C)]
struct PacketHead {
    buf: *mut c_void,
    pts: i64,
    dts: i64,
    data: *mut u8,
    size: c_int,
    stream_index: c_int,
    flags: c_int,
}

#[repr(C)]
struct FrameHead {
    data: [*mut u8; 8],
    linesize: [c_int; 8],
    extended_data: *mut *mut u8,
    width: c_int,
    height: c_int,
    nb_samples: c_int,
    format: c_int,
}

#[repr(C)]
struct ChannelLayout {
    order: c_int,
    nb_channels: c_int,
    mask: u64,
    opaque: *mut c_void,
}

type P = *mut c_void;

/// The functions used, and the libraries they live in.
pub struct Ff {
    _libs: Vec<Library>,
    pub version: String,
    frame_alloc: unsafe extern "C" fn() -> P,
    frame_free: unsafe extern "C" fn(*mut P),
    frame_unref: unsafe extern "C" fn(P),
    opt_get_int: unsafe extern "C" fn(P, *const c_char, c_int, *mut i64) -> c_int,
    opt_get_image_size: unsafe extern "C" fn(P, *const c_char, c_int, *mut c_int, *mut c_int) -> c_int,
    opt_get_chlayout: unsafe extern "C" fn(P, *const c_char, c_int, *mut ChannelLayout) -> c_int,
    channel_layout_uninit: unsafe extern "C" fn(*mut ChannelLayout),
    log_set_level: unsafe extern "C" fn(c_int),
    codec_alloc_context3: unsafe extern "C" fn(*const c_void) -> P,
    codec_free_context: unsafe extern "C" fn(*mut P),
    parameters_to_context: unsafe extern "C" fn(P, *const c_void) -> c_int,
    codec_open2: unsafe extern "C" fn(P, *const c_void, *mut P) -> c_int,
    send_packet: unsafe extern "C" fn(P, *const c_void) -> c_int,
    receive_frame: unsafe extern "C" fn(P, P) -> c_int,
    packet_alloc: unsafe extern "C" fn() -> P,
    packet_free: unsafe extern "C" fn(*mut P),
    packet_unref: unsafe extern "C" fn(P),
    format_open_input: unsafe extern "C" fn(*mut P, *const c_char, *const c_void, *mut P) -> c_int,
    format_find_stream_info: unsafe extern "C" fn(P, *mut P) -> c_int,
    format_close_input: unsafe extern "C" fn(*mut P),
    find_best_stream: unsafe extern "C" fn(P, c_int, c_int, c_int, *mut *const c_void, c_int) -> c_int,
    read_frame: unsafe extern "C" fn(P, P) -> c_int,
    seek_frame: unsafe extern "C" fn(P, c_int, i64, c_int) -> c_int,
    flush_buffers: unsafe extern "C" fn(P),
    pix: [c_int; 3],    // yuv420p, yuvj420p, yuva420p
    smp: [c_int; 4],    // flt, fltp, s16, s16p
}

fn lib_name(dir: &Path, lib: usize, major: u32) -> PathBuf {
    let names = ["avutil", "avcodec", "avformat"];
    let v = if lib == 0 { major.saturating_sub(2) } else { major }; // (major 0: the unversioned name, v unused)
    let file = if cfg!(windows) {
        if major > 0 { format!("{}-{v}.dll", names[lib]) } else { format!("{}.dll", names[lib]) }
    } else if cfg!(target_os = "macos") {
        if major > 0 { format!("lib{}.{v}.dylib", names[lib]) } else { format!("lib{}.dylib", names[lib]) }
    } else if major > 0 {
        format!("lib{}.so.{v}", names[lib])
    } else {
        format!("lib{}.so", names[lib])
    };
    if dir.as_os_str().is_empty() { PathBuf::from(file) } else { dir.join(file) }
}

/// The folders looked in, in order ("" is the system's search).
pub fn search_dirs() -> Vec<PathBuf> {
    if let Some(d) = std::env::var_os("HWVX_FFMPEG_DIR").filter(|d| !d.is_empty()) {
        return vec![PathBuf::from(d)];
    }
    let mut dirs = Vec::new();
    if let Some(exe) = std::env::current_exe().ok().and_then(|e| e.parent().map(Path::to_path_buf)) {
        dirs.push(exe.clone());
        if cfg!(target_os = "macos") {
            dirs.push(exe.join("../Frameworks"));
        } else if cfg!(target_os = "linux") {
            dirs.push(exe.join("../lib"));
        }
    }
    dirs.push(PathBuf::new());
    if cfg!(target_os = "macos") {
        dirs.push("/opt/homebrew/lib".into());
        dirs.push("/usr/local/lib".into());
    }
    dirs
}

macro_rules! sym {
    ($lib:expr, $name:literal) => {{
        let s: libloading::Symbol<'_, _> = unsafe { $lib.get(concat!($name, "\0").as_bytes()) }.map_err(|_| format!("FFmpeg has no {}", $name))?;
        *s
    }};
}

fn try_load(dir: &Path, major: u32) -> Result<Ff, String> {
    let mut libs = Vec::new();
    for lib in 0..3 {
        let p = lib_name(dir, lib, major);
        libs.push(unsafe { Library::new(&p) }.map_err(|e| format!("{}: {e}", p.display()))?);
    }
    let vu: unsafe extern "C" fn() -> c_uint = sym!(libs[0], "avutil_version");
    let vc: unsafe extern "C" fn() -> c_uint = sym!(libs[1], "avcodec_version");
    let vf: unsafe extern "C" fn() -> c_uint = sym!(libs[2], "avformat_version");
    let (u, c, f) = unsafe { (vu(), vc(), vf()) };
    if !(MIN_MAJOR..=MAX_MAJOR).contains(&(f >> 16)) || c >> 16 != f >> 16 || u >> 16 != (f >> 16) - 2 {
        return Err(format!("libavformat {}, libavcodec {}, libavutil {} (need FFmpeg 6-9)", f >> 16, c >> 16, u >> 16));
    }
    let (u0, c0, f0) = (&libs[0], &libs[1], &libs[2]);
    let get_pix: unsafe extern "C" fn(*const c_char) -> c_int = sym!(u0, "av_get_pix_fmt");
    let get_smp: unsafe extern "C" fn(*const c_char) -> c_int = sym!(u0, "av_get_sample_fmt");
    let pix = ["yuv420p", "yuvj420p", "yuva420p"].map(|n| unsafe { get_pix(CString::new(n).unwrap().as_ptr()) });
    let smp = ["flt", "fltp", "s16", "s16p"].map(|n| unsafe { get_smp(CString::new(n).unwrap().as_ptr()) });
    let ff = Ff {
        version: format!("FFmpeg {} (libavformat {}.{}.{})", (f >> 16) as i32 - 54, f >> 16, (f >> 8) & 0xFF, f & 0xFF),
        frame_alloc: sym!(u0, "av_frame_alloc"),
        frame_free: sym!(u0, "av_frame_free"),
        frame_unref: sym!(u0, "av_frame_unref"),
        opt_get_int: sym!(u0, "av_opt_get_int"),
        opt_get_image_size: sym!(u0, "av_opt_get_image_size"),
        opt_get_chlayout: sym!(u0, "av_opt_get_chlayout"),
        channel_layout_uninit: sym!(u0, "av_channel_layout_uninit"),
        log_set_level: sym!(u0, "av_log_set_level"),
        codec_alloc_context3: sym!(c0, "avcodec_alloc_context3"),
        codec_free_context: sym!(c0, "avcodec_free_context"),
        parameters_to_context: sym!(c0, "avcodec_parameters_to_context"),
        codec_open2: sym!(c0, "avcodec_open2"),
        send_packet: sym!(c0, "avcodec_send_packet"),
        receive_frame: sym!(c0, "avcodec_receive_frame"),
        packet_alloc: sym!(c0, "av_packet_alloc"),
        packet_free: sym!(c0, "av_packet_free"),
        packet_unref: sym!(c0, "av_packet_unref"),
        format_open_input: sym!(f0, "avformat_open_input"),
        format_find_stream_info: sym!(f0, "avformat_find_stream_info"),
        format_close_input: sym!(f0, "avformat_close_input"),
        find_best_stream: sym!(f0, "av_find_best_stream"),
        read_frame: sym!(f0, "av_read_frame"),
        seek_frame: sym!(f0, "av_seek_frame"),
        flush_buffers: sym!(c0, "avcodec_flush_buffers"),
        pix,
        smp,
        _libs: libs,
    };
    unsafe { (ff.log_set_level)(LOG_ERROR) };
    Ok(ff)
}

static FF: OnceLock<Result<Ff, String>> = OnceLock::new();

/// FFmpeg, loaded once; Err says why not ("FFmpeg not found ...").
pub fn load() -> Result<&'static Ff, String> {
    FF.get_or_init(|| {
        let mut why = String::new();
        for d in search_dirs() {
            for major in (MIN_MAJOR..=MAX_MAJOR).rev().chain([0]) {
                match try_load(&d, major) {
                    Ok(f) => return Ok(f),
                    Err(e) if e.contains("need FFmpeg") || e.contains("has no") => why = e,
                    Err(_) => {}
                }
            }
        }
        let mut s = "FFmpeg not found (6-9: libavformat, libavcodec, libavutil".to_string();
        if let Some(d) = std::env::var_os("HWVX_FFMPEG_DIR").filter(|d| !d.is_empty()) {
            s += &format!(" in HWVX_FFMPEG_DIR={}", PathBuf::from(d).display());
        }
        s += ")";
        if !why.is_empty() {
            s += &format!(": {why}");
        }
        Err(s)
    })
    .as_ref()
    .map_err(|e| e.clone())
}

/// A movie FFmpeg reads: its video (and optionally its audio) decoded frame by frame.
pub struct FfMovie {
    ff: &'static Ff,
    fmt: P,
    vdec: P,
    adec: P,
    pkt: P,
    frame: P,
    vindex: c_int,
    aindex: c_int,
    pub width: u32,
    pub height: u32,
    pub fps: f64,
    pub frames: u32,
    /// the video's first time stamp, in seconds
    pub start: f64,
    /// the length the container states, in seconds (0: none)
    pub duration: f64,
    pub channels: u32,
    pub rate: u32,
    pub video_codec: String,
    pub audio_codec: String,
    eof: bool,
    flushed: bool,
    /// decoded audio, interleaved float, not yet taken
    pub pcm: Vec<f32>,
    bad_pixels: bool,
    /// after a seek: video packets dropped up to a key frame, whose time stamp is kept here
    seeking: bool,
    key_pts: i64,
}

// the contexts belong to this movie alone; it moves to a decoding thread whole
unsafe impl Send for FfMovie {}

impl Drop for FfMovie {
    fn drop(&mut self) {
        let ff = self.ff;
        unsafe {
            if !self.pkt.is_null() {
                (ff.packet_free)(&mut self.pkt);
            }
            if !self.frame.is_null() {
                (ff.frame_free)(&mut self.frame);
            }
            if !self.vdec.is_null() {
                (ff.codec_free_context)(&mut self.vdec);
            }
            if !self.adec.is_null() {
                (ff.codec_free_context)(&mut self.adec);
            }
            if !self.fmt.is_null() {
                (ff.format_close_input)(&mut self.fmt);
            }
        }
    }
}

fn clamp8(v: i32) -> u8 {
    v.clamp(0, 255) as u8
}

impl FfMovie {
    fn stream(&self, i: c_int) -> &StreamHead {
        unsafe { &**(*(self.fmt as *const FormatHead)).streams.add(i as usize) }
    }

    fn open_decoder(&self, index: c_int, codec: *const c_void) -> P {
        let ff = self.ff;
        unsafe {
            let mut dec = (ff.codec_alloc_context3)(codec);
            if dec.is_null() {
                return dec;
            }
            if (ff.parameters_to_context)(dec, self.stream(index).codecpar) < 0 || (ff.codec_open2)(dec, codec, std::ptr::null_mut()) < 0 {
                (ff.codec_free_context)(&mut dec);
                return std::ptr::null_mut();
            }
            dec
        }
    }

    /// An AVCodec's name: its first field (AVCodec starts with `name` in every release).
    fn codec_name(codec: *const c_void) -> String {
        unsafe {
            let p = *(codec as *const *const c_char);
            if p.is_null() { "?".into() } else { std::ffi::CStr::from_ptr(p).to_string_lossy().into_owned() }
        }
    }

    /// Opens a file: its video always, its audio if `audio`. `video` false: the video stream is
    /// found (for its clock) but not decoded.
    pub fn open(path: &Path, video: bool, audio: bool) -> Result<FfMovie, String> {
        let ff = load()?;
        let cpath = CString::new(path.to_string_lossy().as_bytes()).map_err(|_| "a path with a NUL".to_string())?;
        let mut m = FfMovie {
            ff,
            fmt: std::ptr::null_mut(),
            vdec: std::ptr::null_mut(),
            adec: std::ptr::null_mut(),
            pkt: std::ptr::null_mut(),
            frame: std::ptr::null_mut(),
            vindex: -1,
            aindex: -1,
            width: 0,
            height: 0,
            fps: 0.0,
            frames: 0,
            start: 0.0,
            duration: 0.0,
            channels: 0,
            rate: 0,
            video_codec: String::new(),
            audio_codec: String::new(),
            eof: false,
            flushed: false,
            pcm: Vec::new(),
            bad_pixels: false,
            seeking: false,
            key_pts: NOPTS,
        };
        unsafe {
            if (ff.format_open_input)(&mut m.fmt, cpath.as_ptr(), std::ptr::null(), std::ptr::null_mut()) < 0 {
                m.fmt = std::ptr::null_mut();
                return Err(format!("{}: not a movie this FFmpeg reads", path.display()));
            }
            // fills in what a header doesn't say (an MPEG program stream's size and rate)
            (ff.format_find_stream_info)(m.fmt, std::ptr::null_mut());
            let mut vcodec: *const c_void = std::ptr::null();
            m.vindex = (ff.find_best_stream)(m.fmt, MEDIA_VIDEO, -1, -1, &mut vcodec, 0);
            if m.vindex < 0 || vcodec.is_null() {
                return Err(format!("{}: no video this FFmpeg decodes", path.display()));
            }
            // the decoder is opened even when only the audio is wanted: it says the picture's size
            m.vdec = m.open_decoder(m.vindex, vcodec);
            if m.vdec.is_null() {
                return Err(format!("{}: the video decoder doesn't open", path.display()));
            }
            m.video_codec = FfMovie::codec_name(vcodec);
            let (mut w, mut h) = (0, 0);
            let vs = CString::new("video_size").unwrap();
            (ff.opt_get_image_size)(m.vdec, vs.as_ptr(), 0, &mut w, &mut h);
            (m.width, m.height) = (w.max(0) as u32, h.max(0) as u32);
            let st = &*(m.stream(m.vindex) as *const StreamHead);
            // Bink's time base is its frame clock; MPEG's is 90 kHz, the rate in avg_frame_rate
            let mut fps = if st.tb_num > 0 && st.tb_den > 0 { st.tb_den as f64 / st.tb_num as f64 } else { 0.0 };
            if !(1.0..=120.0).contains(&fps) && st.avg_num > 0 && st.avg_den > 0 {
                fps = st.avg_num as f64 / st.avg_den as f64;
            }
            if !(1.0..=120.0).contains(&fps) {
                fps = 29.97;
            }
            m.fps = fps;
            if st.tb_num > 0 && st.tb_den > 0 {
                let tb = st.tb_num as f64 / st.tb_den as f64;
                m.start = if st.start_time != NOPTS { st.start_time as f64 * tb } else { 0.0 };
                if st.duration > 0 && st.duration != NOPTS {
                    m.duration = st.duration as f64 * tb;
                }
                if st.nb_frames > 0 {
                    m.frames = st.nb_frames as u32;
                } else if m.duration > 0.0 {
                    m.frames = (m.duration * fps + 0.5) as u32;
                }
            }
            if m.duration == 0.0 && m.frames > 0 {
                m.duration = m.frames as f64 / fps;
            }
            if !video {
                (ff.codec_free_context)(&mut m.vdec);
            }
            let mut acodec: *const c_void = std::ptr::null();
            let ai = (ff.find_best_stream)(m.fmt, MEDIA_AUDIO, -1, m.vindex, &mut acodec, 0);
            if ai >= 0 && !acodec.is_null() {
                let dec = m.open_decoder(ai, acodec);
                if !dec.is_null() {
                    let mut rate = 0i64;
                    let mut cl = ChannelLayout { order: 0, nb_channels: 0, mask: 0, opaque: std::ptr::null_mut() };
                    let (ar, chl) = (CString::new("ar").unwrap(), CString::new("ch_layout").unwrap());
                    (ff.opt_get_int)(dec, ar.as_ptr(), 0, &mut rate);
                    if (ff.opt_get_chlayout)(dec, chl.as_ptr(), 0, &mut cl) >= 0 {
                        m.channels = cl.nb_channels.max(0) as u32;
                        (ff.channel_layout_uninit)(&mut cl);
                    }
                    m.rate = rate.max(0) as u32;
                    m.audio_codec = FfMovie::codec_name(acodec);
                    let mut dec = dec;
                    if !(1..=8).contains(&m.channels) || !(4000..=192000).contains(&m.rate) || !audio {
                        (ff.codec_free_context)(&mut dec);
                        if !(1..=8).contains(&m.channels) || !(4000..=192000).contains(&m.rate) {
                            m.channels = 0;
                            m.rate = 0;
                        }
                    } else {
                        m.adec = dec;
                        m.aindex = ai;
                    }
                }
            }
            m.pkt = (ff.packet_alloc)();
            m.frame = (ff.frame_alloc)();
        }
        if m.pkt.is_null() || m.frame.is_null() || m.width == 0 || m.height == 0 {
            return Err(format!("{}: FFmpeg can't decode it ({}x{})", path.display(), m.width, m.height));
        }
        Ok(m)
    }

    /// The audio decoder's channels now: a stream may change its layout midway (mono to stereo),
    /// and each frame has the decoder's layout of its time.
    fn frame_channels(&self) -> usize {
        let ff = self.ff;
        let mut cl = ChannelLayout { order: 0, nb_channels: 0, mask: 0, opaque: std::ptr::null_mut() };
        let chl = c"ch_layout";
        unsafe {
            if (ff.opt_get_chlayout)(self.adec, chl.as_ptr(), 0, &mut cl) < 0 {
                return 0;
            }
            (ff.channel_layout_uninit)(&mut cl);
        }
        cl.nb_channels.max(0) as usize
    }

    fn take_audio(&mut self) {
        let ff = self.ff;
        let f = unsafe { &*(self.frame as *const FrameHead) };
        let (n, ch, fch) = (f.nb_samples.max(0) as usize, self.channels as usize, self.frame_channels());
        if n == 0 || ch == 0 || fch == 0 {
            return;
        }
        // the frame's channels to the track's: one channel repeated, the extra ones dropped
        let src = |c: usize| c.min(fch - 1);
        unsafe {
            let plane = |c: usize| *f.extended_data.add(c);
            if f.format == ff.smp[1] {
                for i in 0..n {
                    for c in 0..ch {
                        self.pcm.push(*(plane(src(c)) as *const f32).add(i));
                    }
                }
            } else if f.format == ff.smp[0] {
                let s = std::slice::from_raw_parts(plane(0) as *const f32, n * fch);
                for i in 0..n {
                    for c in 0..ch {
                        self.pcm.push(s[i * fch + src(c)]);
                    }
                }
            } else if f.format == ff.smp[3] {
                for i in 0..n {
                    for c in 0..ch {
                        self.pcm.push(*(plane(src(c)) as *const i16).add(i) as f32 / 32768.0);
                    }
                }
            } else if f.format == ff.smp[2] {
                let s = std::slice::from_raw_parts(plane(0) as *const i16, n * fch);
                for i in 0..n {
                    for c in 0..ch {
                        self.pcm.push(s[i * fch + src(c)] as f32 / 32768.0);
                    }
                }
            }
        }
    }

    fn decode_audio(&mut self, pkt: P) {
        if self.adec.is_null() {
            return;
        }
        let ff = self.ff;
        unsafe {
            if (ff.send_packet)(self.adec, pkt) < 0 {
                return;
            }
            while (ff.receive_frame)(self.adec, self.frame) == 0 {
                self.take_audio();
                (ff.frame_unref)(self.frame);
            }
        }
    }

    /// Reads one packet: video sent to the decoder (Some(true): it took it), audio decoded at once.
    /// None at the end.
    fn read_packet(&mut self) -> Option<bool> {
        let ff = self.ff;
        if self.eof {
            return None;
        }
        unsafe {
            if (ff.read_frame)(self.fmt, self.pkt) < 0 {
                self.eof = true;
                self.decode_audio(std::ptr::null_mut());
                return None;
            }
            let ph = &*(self.pkt as *const PacketHead);
            let idx = ph.stream_index;
            let mut sent = false;
            if idx == self.vindex && self.seeking && ph.flags & PKT_KEY == 0 {
                // a seek's pictures start at a key frame
            } else if idx == self.vindex && !self.vdec.is_null() {
                if self.seeking {
                    self.seeking = false;
                    self.key_pts = if ph.pts != NOPTS { ph.pts } else { ph.dts };
                }
                (ff.send_packet)(self.vdec, self.pkt); // a packet the decoder refuses is dropped
                sent = true;
            } else if idx == self.aindex {
                self.decode_audio(self.pkt);
            }
            (ff.packet_unref)(self.pkt);
            Some(sent)
        }
    }

    /// To the key frame at or before `frame` (the video's clock): the frame it lands on, whose
    /// picture `next_frame` gives next. None: no seek (the container can't, or its key frame has
    /// no time stamp); the movie is then to be opened again.
    pub fn seek(&mut self, frame: u32) -> Option<u32> {
        if self.vdec.is_null() {
            return None;
        }
        let st = self.stream(self.vindex);
        if st.tb_num <= 0 || st.tb_den <= 0 {
            return None;
        }
        let tb = st.tb_num as f64 / st.tb_den as f64;
        let start = if st.start_time != NOPTS { st.start_time } else { 0 };
        let target = frame as f64 / self.fps;
        // a program stream lands on the key frame after its time stamp search: earlier and earlier
        for back in [0.0, 1.0, 2.0, 4.0, 8.0] {
            let ts = start + ((target - back).max(0.0) / tb) as i64;
            match self.seek_to(ts, start, tb) {
                Some(at) if at <= frame => return Some(at),
                Some(_) if back < target => {}
                _ => return None,
            }
        }
        None
    }

    /// To the key frame `ts` (stream time) finds: its frame. None: no seek.
    fn seek_to(&mut self, ts: i64, start: i64, tb: f64) -> Option<u32> {
        let ff = self.ff;
        unsafe {
            if (ff.seek_frame)(self.fmt, self.vindex, ts, SEEK_BACKWARD) < 0 {
                return None;
            }
            (ff.flush_buffers)(self.vdec);
            if !self.adec.is_null() {
                (ff.flush_buffers)(self.adec);
            }
        }
        self.pcm.clear();
        (self.eof, self.flushed, self.seeking, self.key_pts) = (false, false, true, NOPTS);
        // up to the key frame's packet, sent to the decoder
        while self.seeking {
            self.read_packet()?;
        }
        if self.key_pts == NOPTS {
            return None;
        }
        Some(((self.key_pts - start) as f64 * tb * self.fps).round().max(0.0) as u32)
    }

    /// The next picture: true if there was one (converted to RGBA into `rgba` when given, top row
    /// first, width * height * 4 bytes).
    pub fn next_frame(&mut self, rgba: Option<&mut [u8]>) -> bool {
        let ff = self.ff;
        if self.vdec.is_null() {
            return false;
        }
        let mut rgba = rgba;
        loop {
            let r = unsafe { (ff.receive_frame)(self.vdec, self.frame) };
            if r == 0 {
                if let Some(out) = rgba.take() {
                    let f = unsafe { &*(self.frame as *const FrameHead) };
                    if ff.pix.contains(&f.format) {
                        self.to_rgba(f, f.format == ff.pix[1], out);
                    } else if !self.bad_pixels {
                        self.bad_pixels = true;
                        eprintln!("ffmpeg: pixel format {} not handled", f.format);
                    }
                }
                unsafe { (ff.frame_unref)(self.frame) };
                return true;
            }
            if r != -EAGAIN {
                return false; // the end, or an error
            }
            match self.read_packet() {
                Some(_) => {}
                None => {
                    if self.flushed {
                        return false;
                    }
                    unsafe { (ff.send_packet)(self.vdec, std::ptr::null()) };
                    self.flushed = true;
                }
            }
        }
    }

    /// The whole audio track, interleaved float (the video packets passed over).
    pub fn all_audio(&mut self) -> Vec<f32> {
        self.all_audio_until(&std::sync::atomic::AtomicBool::new(false)).unwrap_or_default()
    }

    /// The whole audio track, or None once `stop` is set.
    pub fn all_audio_until(&mut self, stop: &std::sync::atomic::AtomicBool) -> Option<Vec<f32>> {
        while self.read_packet().is_some() {
            if stop.load(std::sync::atomic::Ordering::Relaxed) {
                return None;
            }
        }
        Some(std::mem::take(&mut self.pcm))
    }

    /// 4:2:0 planes to RGBA: BT.601, video range, or full range for yuvj420p (bink.c ToRGBA).
    fn to_rgba(&self, f: &FrameHead, full: bool, out: &mut [u8]) {
        let (width, height) = (self.width as usize, self.height as usize);
        let w = (f.width.max(0) as usize).min(width);
        let h = (f.height.max(0) as usize).min(height);
        let (ky, kr, kgu, kgv, kb, y0) = if full { (256, 359, 88, 183, 454, 0) } else { (298, 409, 100, 208, 516, 16) };
        for y in 0..h {
            unsafe {
                let py = f.data[0].add(y * f.linesize[0] as usize);
                let pu = f.data[1].add((y >> 1) * f.linesize[1] as usize);
                let pv = f.data[2].add((y >> 1) * f.linesize[2] as usize);
                let o = &mut out[y * width * 4..(y * width + w) * 4];
                for x in 0..w {
                    let c = ky * (*py.add(x) as i32 - y0) + 128;
                    let d = *pu.add(x >> 1) as i32 - 128;
                    let e = *pv.add(x >> 1) as i32 - 128;
                    o[x * 4] = clamp8((c + kr * e) >> 8);
                    o[x * 4 + 1] = clamp8((c - kgu * d - kgv * e) >> 8);
                    o[x * 4 + 2] = clamp8((c + kb * d) >> 8);
                    o[x * 4 + 3] = 255;
                }
            }
        }
    }
}
