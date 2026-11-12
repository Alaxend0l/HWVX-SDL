//! Bytes written as text (xdl_mod.text_blobs, json_blobs, number_blobs): runs of hex and
//! base64, data URIs, arrays of numbers, with Python 3.9's base64 decoding.

use regex::Regex;
use serde_json::Value;
use std::sync::OnceLock;

const MIN_BLOB: usize = 32;
/// Python's \s (str.isspace): Unicode White_Space and the four separators 0x1C-0x1F
const WS: &str = r"\s\x1C-\x1F";

fn hex_run() -> &'static Regex {
    static R: OnceLock<Regex> = OnceLock::new();
    R.get_or_init(|| {
        Regex::new(&format!(r"(?:0[xX])?[0-9a-fA-F]{{2,}}(?:[{WS},;:]+(?:0[xX])?[0-9a-fA-F]{{2,}})*")).unwrap()
    })
}
fn hex_run_full() -> &'static Regex {
    static R: OnceLock<Regex> = OnceLock::new();
    R.get_or_init(|| {
        Regex::new(&format!(r"\A(?:0[xX])?[0-9a-fA-F]{{2,}}(?:[{WS},;:]+(?:0[xX])?[0-9a-fA-F]{{2,}})*\z")).unwrap()
    })
}
fn hex_tok() -> &'static Regex {
    static R: OnceLock<Regex> = OnceLock::new();
    R.get_or_init(|| Regex::new(r"(?:0[xX])?([0-9a-fA-F]+)").unwrap())
}
fn b64_run() -> &'static Regex {
    static R: OnceLock<Regex> = OnceLock::new();
    R.get_or_init(|| Regex::new(&format!(r"[A-Za-z0-9+/]{{16,}}={{0,2}}(?:[{WS}]*[A-Za-z0-9+/]{{4,}}={{0,2}})*")).unwrap())
}

fn is_py_space(c: char) -> bool {
    c.is_whitespace() || ('\x1c'..='\x1f').contains(&c)
}

fn unhex(s: &str) -> Vec<u8> {
    (0..s.len() / 2).map(|i| u8::from_str_radix(&s[2 * i..2 * i + 2], 16).unwrap()).collect()
}

/// binascii.a2b_base64 of Python 3.9 (base64.b64decode with validate=False): characters
/// outside the alphabet are skipped, a pad ends the input once a quad has two characters.
pub fn b64decode(s: &str) -> Result<Vec<u8>, String> {
    if !s.is_ascii() {
        return Err("string argument should contain only ASCII characters".into());
    }
    fn val(c: u8) -> Option<u32> {
        match c {
            b'A'..=b'Z' => Some((c - b'A') as u32),
            b'a'..=b'z' => Some((c - b'a' + 26) as u32),
            b'0'..=b'9' => Some((c - b'0' + 52) as u32),
            b'+' => Some(62),
            b'/' => Some(63),
            _ => None,
        }
    }
    let b = s.as_bytes();
    let (mut out, mut leftbits, mut leftchar, mut quad) = (vec![], 0u32, 0u32, 0u32);
    for (i, &c) in b.iter().enumerate() {
        if c > 0x7f || c == b'\r' || c == b'\n' || c == b' ' {
            continue;
        }
        if c == b'=' {
            // binascii_find_valid(.., 1): the next character valid in its table ('=' counts)
            let next = b[i + 1..].iter().copied().find(|&x| x <= 0x7f && (val(x).is_some() || x == b'='));
            if quad < 2 || (quad == 2 && next != Some(b'=')) {
                continue;
            }
            leftbits = 0;
            break;
        }
        let Some(v) = val(c) else { continue };
        quad = (quad + 1) & 3;
        leftchar = (leftchar << 6) | v;
        leftbits += 6;
        if leftbits >= 8 {
            leftbits -= 8;
            out.push(((leftchar >> leftbits) & 0xff) as u8);
            leftchar &= (1 << leftbits) - 1;
        }
    }
    if leftbits != 0 {
        if leftbits == 6 {
            return Err(format!(
                "Invalid base64-encoded string: number of data characters ({}) cannot be 1 more than a multiple of 4",
                out.len() / 3 * 4 + 1
            ));
        }
        return Err("Incorrect padding".into());
    }
    Ok(out)
}

fn char_index(s: &str, byte: usize) -> usize {
    s[..byte].chars().count()
}

/// text_blobs: (what, bytes) of each run of hex or base64 in a text
pub fn text_blobs(s: &str) -> Vec<(String, Vec<u8>)> {
    let mut out = vec![];
    for m in hex_run().find_iter(s) {
        let toks: Vec<&str> = hex_tok().captures_iter(m.as_str()).map(|c| c.get(1).unwrap().as_str()).collect();
        if toks.iter().map(|t| t.len()).sum::<usize>() < 2 * MIN_BLOB || toks.iter().any(|t| t.len() % 2 != 0) {
            continue;
        }
        let at = char_index(s, m.start());
        out.push((format!("hex at {at}"), unhex(&toks.concat())));
        let w = toks[0].len();
        if (w == 4 || w == 8) && toks.iter().all(|t| t.len() == w) {
            out.push((format!("hex words at {at}"), toks.iter().flat_map(|t| unhex(t).into_iter().rev()).collect()));
        }
    }
    for m in b64_run().find_iter(s) {
        let mut t: String = m.as_str().chars().filter(|c| !is_py_space(*c)).collect();
        if t.chars().count() < 44 || hex_run_full().is_match(m.as_str()) {
            continue;
        }
        t = t.trim_end_matches('=').to_string();
        if t.len() % 4 == 1 {
            t.truncate(t.len() - 1);
        } else {
            let pad = (4 - t.len() % 4) % 4;
            t.push_str(&"=".repeat(pad));
        }
        if let Ok(raw) = b64decode(&t)
            && raw.len() >= MIN_BLOB
        {
            out.push((format!("base64 at {}", char_index(s, m.start())), raw));
        }
    }
    out
}

#[derive(Clone, Copy)]
enum Num {
    Int(i128),
    Float(f64),
}

fn num_of(v: &Value) -> Option<Num> {
    match v {
        Value::Number(n) => Some(if let Some(i) = n.as_i64() {
            Num::Int(i as i128)
        } else if let Some(u) = n.as_u64() {
            Num::Int(u as i128)
        } else {
            Num::Float(n.as_f64().unwrap())
        }),
        _ => None,
    }
}

/// _numbers: the numbers of an array of numbers (or of arrays of them), or None
fn numbers(node: &[Value]) -> Option<Vec<Num>> {
    let mut out = vec![];
    for v in node {
        match v {
            Value::Array(a) => out.extend(numbers(a)?),
            _ => out.push(num_of(v)?),
        }
    }
    Some(out)
}

/// number_blobs: u8, s8, f32 LE, f32 BE
fn number_blobs(values: &[Num]) -> Vec<(&'static str, Vec<u8>)> {
    let mut out = vec![];
    let ints: Option<Vec<i128>> = values.iter().map(|v| if let Num::Int(i) = v { Some(*i) } else { None }).collect();
    if let Some(iv) = &ints {
        if iv.iter().all(|&v| (0..=255).contains(&v)) && values.len() >= MIN_BLOB {
            out.push(("u8", iv.iter().map(|&v| v as u8).collect()));
        }
        if iv.iter().all(|&v| (-128..=127).contains(&v)) && values.len() >= MIN_BLOB && iv.iter().any(|&v| v < 0) {
            out.push(("s8", iv.iter().map(|&v| (v & 0xFF) as u8).collect()));
        }
    }
    let f = |v: &Num| match v {
        Num::Int(i) => *i as f64,
        Num::Float(x) => *x,
    };
    let fits = values.iter().all(|v| match v {
        Num::Int(i) => i.unsigned_abs() as f64 <= 3.4028234663852886e38,
        Num::Float(x) => x.abs() <= 3.4028234663852886e38,
    });
    if values.len() * 4 >= MIN_BLOB && fits {
        let a: Vec<f32> = values.iter().map(|v| f(v) as f32).collect();
        out.push(("f32 LE", a.iter().flat_map(|x| x.to_le_bytes()).collect()));
        out.push(("f32 BE", a.iter().flat_map(|x| x.to_be_bytes()).collect()));
    }
    out
}

/// json_blobs: (path, bytes) of everything in a JSON tree that may hold bytes
pub fn json_blobs(node: &Value, path: &str, out: &mut Vec<(String, Vec<u8>)>) {
    match node {
        Value::Object(o) => {
            for (k, v) in o {
                let p = if path.is_empty() { k.clone() } else { format!("{path}.{k}") };
                json_blobs(v, &p, out);
            }
        }
        Value::Array(a) => {
            let nums = if a.is_empty() { None } else { numbers(a) };
            if let Some(nums) = nums {
                for (what, raw) in number_blobs(&nums) {
                    out.push((format!("{path} ({what})"), raw));
                }
                return;
            }
            for (i, v) in a.iter().enumerate() {
                json_blobs(v, &format!("{path}[{i}]"), out);
            }
        }
        Value::String(s) => {
            let head: String = s.chars().take(200).collect();
            if s.starts_with("data:") && head.contains(";base64,") {
                if let Ok(raw) = b64decode(s.split_once(";base64,").unwrap().1) {
                    out.push((path.to_string(), raw));
                }
                return;
            }
            for (what, raw) in text_blobs(s) {
                out.push((format!("{path} ({what})"), raw));
            }
        }
        _ => {}
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn base64_like_python_39() {
        assert_eq!(b64decode("aGVsbG8=").unwrap(), b"hello");
        assert_eq!(b64decode("aGVsbG8").unwrap_err(), "Incorrect padding");
        assert_eq!(b64decode("aGVs=bG8=").unwrap(), b"hello");
        assert!(b64decode("a").unwrap_err().contains("1 more than a multiple of 4"));
        assert_eq!(b64decode("aGVsbA==ZZZZ").unwrap(), b"hell");
    }
}
