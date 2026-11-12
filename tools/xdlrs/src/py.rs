//! Python's text and comparison rules for JSON values, so the tools write what the Python
//! tools write: `repr(float)`, `json.dumps` (ensure_ascii, its separators, `indent`,
//! `sort_keys`), `str()`/`repr()` of a value in messages, and `==` (1 == 1.0 == True).
//!
//! f32 fields: xdl_struct's `Num` is a float that writes as the shortest text that reads back
//! as the same f32 (`f32_text`). Here a decoded f32 is the value that text reads back as
//! (`num_value`): an integer when the text is one ("7400", "0"), else the f64 of the text
//! ("0.3"). `dumps` of that value gives the Num's text, so the JSON written is the same, and the
//! value equals what `json.loads` of the written file gives (which is what every Python caller
//! works on once a file is saved). Packed back as f32 it is the same bits.

use crate::error::{Error, Result};
use serde_json::{Map, Number, Value};

/// Python `repr(float)` (the shortest text that reads back as the same f64).
pub fn float_repr(x: f64) -> String {
    if x.is_nan() {
        return "nan".into();
    }
    if x.is_infinite() {
        return if x < 0.0 { "-inf".into() } else { "inf".into() };
    }
    if x == 0.0 {
        return if x.is_sign_negative() { "-0.0".into() } else { "0.0".into() };
    }
    // shortest round-trip digits and the exponent of the first one
    let e = format!("{:e}", x);
    let (mant, exp) = e.split_once('e').unwrap();
    let exp: i32 = exp.parse().unwrap();
    let neg = mant.starts_with('-');
    let digits: String = mant.chars().filter(|c| c.is_ascii_digit()).collect();
    let mut out = String::new();
    if neg {
        out.push('-');
    }
    let decpt = exp + 1;
    if decpt <= -4 || decpt > 16 {
        out.push_str(&digits[..1]);
        if digits.len() > 1 {
            out.push('.');
            out.push_str(&digits[1..]);
        }
        out.push('e');
        out.push(if exp < 0 { '-' } else { '+' });
        out.push_str(&format!("{:02}", exp.abs()));
    } else if decpt <= 0 {
        out.push_str("0.");
        for _ in 0..(-decpt) {
            out.push('0');
        }
        out.push_str(&digits);
    } else {
        let d = decpt as usize;
        if digits.len() <= d {
            out.push_str(&digits);
            for _ in digits.len()..d {
                out.push('0');
            }
            out.push_str(".0");
        } else {
            out.push_str(&digits[..d]);
            out.push('.');
            out.push_str(&digits[d..]);
        }
    }
    out
}

/// xdl_struct.f32_text: the shortest text that reads back as the same f32 (`double`: the f64's
/// repr), "0" / "-0.0" for zeros, no ".0" on whole numbers. NaN and infinities have no JSON form.
pub fn f32_text(v: f64, double: bool) -> Result<String> {
    if v.is_nan() || v.is_infinite() {
        return Err(Error::xdl(format!("{} has no JSON form", float_repr(v))));
    }
    if v == 0.0 {
        return Ok(if v.is_sign_negative() { "-0.0".into() } else { "0".into() });
    }
    if double {
        return Ok(float_repr(v));
    }
    let bits = (v as f32).to_bits();
    let mut s = String::new();
    for p in 1..10 {
        s = format!("{:.*e}", p - 1, v);
        let back: f64 = s.parse().unwrap();
        let f = back as f32;
        if f.is_infinite() {
            // struct.pack("<f", float(s)) raises OverflowError (uncaught in the Python)
            return Err(Error::uncaught("float too large to pack with f format"));
        }
        if f.to_bits() == bits {
            break;
        }
    }
    let mut s = float_repr(s.parse::<f64>().unwrap());
    if s.ends_with(".0") {
        s.truncate(s.len() - 2);
    }
    Ok(s)
}

/// The JSON value a number's text reads back as in Python: an int when it is written as one.
pub fn num_value(text: &str) -> Value {
    if !text.contains(['.', 'e', 'E', 'n', 'N', 'i']) {
        if let Ok(i) = text.parse::<i64>() {
            return Value::from(i);
        }
        if let Ok(u) = text.parse::<u64>() {
            return Value::from(u);
        }
    }
    let f: f64 = text.parse().unwrap_or(f64::NAN);
    Number::from_f64(f).map(Value::Number).unwrap_or(Value::Null)
}

/// A decoded f32 / f64 field as the JSON value (see the module comment).
pub fn float_value(v: f64, double: bool) -> Result<Value> {
    Ok(num_value(&f32_text(v, double)?))
}

/// json.dumps of a string (ensure_ascii).
pub fn json_str(s: &str) -> String {
    let mut o = String::with_capacity(s.len() + 2);
    o.push('"');
    for c in s.chars() {
        match c {
            '"' => o.push_str("\\\""),
            '\\' => o.push_str("\\\\"),
            '\n' => o.push_str("\\n"),
            '\r' => o.push_str("\\r"),
            '\t' => o.push_str("\\t"),
            '\u{8}' => o.push_str("\\b"),
            '\u{c}' => o.push_str("\\f"),
            ' '..='~' => o.push(c),
            _ => {
                let mut buf = [0u16; 2];
                for u in c.encode_utf16(&mut buf) {
                    o.push_str(&format!("\\u{:04x}", u));
                }
            }
        }
    }
    o.push('"');
    o
}

fn json_float(f: f64) -> String {
    if f.is_nan() {
        "NaN".into()
    } else if f.is_infinite() {
        if f < 0.0 { "-Infinity".into() } else { "Infinity".into() }
    } else {
        float_repr(f)
    }
}

/// json.dumps of a number.
pub fn json_num(n: &Number) -> String {
    if let Some(i) = n.as_i64() {
        i.to_string()
    } else if let Some(u) = n.as_u64() {
        u.to_string()
    } else {
        json_float(n.as_f64().unwrap_or(f64::NAN))
    }
}

/// json.dumps(v) with the default separators (", ", ": ").
pub fn dumps(v: &Value) -> String {
    let mut s = String::new();
    dump_compact(v, &mut s);
    s
}

fn dump_compact(v: &Value, s: &mut String) {
    match v {
        Value::Null => s.push_str("null"),
        Value::Bool(b) => s.push_str(if *b { "true" } else { "false" }),
        Value::Number(n) => s.push_str(&json_num(n)),
        Value::String(x) => s.push_str(&json_str(x)),
        Value::Array(a) => {
            s.push('[');
            for (i, x) in a.iter().enumerate() {
                if i > 0 {
                    s.push_str(", ");
                }
                dump_compact(x, s);
            }
            s.push(']');
        }
        Value::Object(m) => {
            s.push('{');
            for (i, (k, x)) in m.iter().enumerate() {
                if i > 0 {
                    s.push_str(", ");
                }
                s.push_str(&json_str(k));
                s.push_str(": ");
                dump_compact(x, s);
            }
            s.push('}');
        }
    }
}

/// json.dumps(v, indent=n, sort_keys=...) (separators "," and ": ").
pub fn dumps_indent(v: &Value, indent: usize, sort_keys: bool) -> String {
    let mut s = String::new();
    dump_indent(v, indent, sort_keys, 0, &mut s);
    s
}

fn dump_indent(v: &Value, ind: usize, sort: bool, level: usize, s: &mut String) {
    let nl = |s: &mut String, l: usize| {
        s.push('\n');
        for _ in 0..ind * l {
            s.push(' ');
        }
    };
    match v {
        Value::Array(a) if !a.is_empty() => {
            s.push('[');
            for (i, x) in a.iter().enumerate() {
                if i > 0 {
                    s.push(',');
                }
                nl(s, level + 1);
                dump_indent(x, ind, sort, level + 1, s);
            }
            nl(s, level);
            s.push(']');
        }
        Value::Object(m) if !m.is_empty() => {
            s.push('{');
            let mut items: Vec<(&String, &Value)> = m.iter().collect();
            if sort {
                items.sort_by(|a, b| a.0.cmp(b.0));
            }
            for (i, (k, x)) in items.into_iter().enumerate() {
                if i > 0 {
                    s.push(',');
                }
                nl(s, level + 1);
                s.push_str(&json_str(k));
                s.push_str(": ");
                dump_indent(x, ind, sort, level + 1, s);
            }
            nl(s, level);
            s.push('}');
        }
        _ => dump_compact(v, s),
    }
}

/// Python repr() of a str.
pub fn str_repr(x: &str) -> String {
    let q = if x.contains('\'') && !x.contains('"') { '"' } else { '\'' };
    let mut o = String::new();
    o.push(q);
    for c in x.chars() {
        match c {
            '\\' => o.push_str("\\\\"),
            '\n' => o.push_str("\\n"),
            '\r' => o.push_str("\\r"),
            '\t' => o.push_str("\\t"),
            c if c == q => {
                o.push('\\');
                o.push(c);
            }
            c if (c as u32) < 0x20 || (0x7f..0xa0).contains(&(c as u32)) => o.push_str(&format!("\\x{:02x}", c as u32)),
            c => o.push(c),
        }
    }
    o.push(q);
    o
}

/// Python repr() of a JSON value (dicts and lists of them as Python prints them).
pub fn repr(v: &Value) -> String {
    match v {
        Value::Null => "None".into(),
        Value::Bool(b) => if *b { "True" } else { "False" }.into(),
        Value::Number(n) => {
            if n.is_f64() {
                float_repr(n.as_f64().unwrap())
            } else {
                json_num(n)
            }
        }
        Value::String(s) => str_repr(s),
        Value::Array(a) => format!("[{}]", a.iter().map(repr).collect::<Vec<_>>().join(", ")),
        Value::Object(m) => format!(
            "{{{}}}",
            m.iter().map(|(k, x)| format!("{}: {}", str_repr(k), repr(x))).collect::<Vec<_>>().join(", ")
        ),
    }
}

/// Python str() of a JSON value (f"{v}").
pub fn str_of(v: &Value) -> String {
    match v {
        Value::String(s) => s.clone(),
        _ => repr(v),
    }
}

/// The Python type name of a JSON value (for TypeError messages).
pub fn type_name(v: &Value) -> &'static str {
    match v {
        Value::Null => "NoneType",
        Value::Bool(_) => "bool",
        Value::Number(n) if n.is_f64() => "float",
        Value::Number(_) => "int",
        Value::String(_) => "str",
        Value::Array(_) => "list",
        Value::Object(_) => "dict",
    }
}

/// A number as Python sees it: int (bool counts as 0/1) or float.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Num {
    Int(i128),
    Float(f64),
}

impl Num {
    pub fn of(v: &Value) -> Option<Num> {
        match v {
            Value::Bool(b) => Some(Num::Int(*b as i128)),
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
    pub fn f64(self) -> f64 {
        match self {
            Num::Int(i) => i as f64,
            Num::Float(f) => f,
        }
    }
    /// Python <, == between int and float (exact for the ints JSON gives)
    pub fn py_lt(self, o: Num) -> bool {
        match (self, o) {
            (Num::Int(a), Num::Int(b)) => a < b,
            _ => self.f64() < o.f64(),
        }
    }
    pub fn py_eq(self, o: Num) -> bool {
        match (self, o) {
            (Num::Int(a), Num::Int(b)) => a == b,
            _ => self.f64() == o.f64(),
        }
    }
}

/// Python == of two JSON values (1 == 1.0 == True, dicts regardless of order).
pub fn eq(a: &Value, b: &Value) -> bool {
    match (a, b) {
        (Value::Null, Value::Null) => true,
        (Value::String(x), Value::String(y)) => x == y,
        (Value::Array(x), Value::Array(y)) => x.len() == y.len() && x.iter().zip(y).all(|(p, q)| eq(p, q)),
        (Value::Object(x), Value::Object(y)) => {
            x.len() == y.len() && x.iter().all(|(k, v)| y.get(k).is_some_and(|w| eq(v, w)))
        }
        _ => match (Num::of(a), Num::of(b)) {
            (Some(p), Some(q)) => p.py_eq(q),
            _ => false,
        },
    }
}

/// An empty JSON object.
pub fn obj() -> Map<String, Value> {
    Map::new()
}

/// Python int(x, 16) of the schemas' "0x1C" texts.
pub fn hex(s: &str) -> Result<i64> {
    let t = s.trim();
    let (neg, t) = match t.strip_prefix('-') {
        Some(r) => (true, r),
        None => (false, t.strip_prefix('+').unwrap_or(t)),
    };
    let t = t.strip_prefix("0x").or_else(|| t.strip_prefix("0X")).unwrap_or(t);
    let t: String = t.chars().filter(|c| *c != '_').collect();
    i64::from_str_radix(&t, 16)
        .map(|v| if neg { -v } else { v })
        .map_err(|_| Error::uncaught(format!("invalid literal for int() with base 16: {}", str_repr(s))))
}

/// Python float(str): what Encoder's float(v) makes of a string.
pub fn float_of_str(s: &str) -> Option<f64> {
    let t = s.trim().replace('_', "");
    let l = t.to_ascii_lowercase();
    let body = l.trim_start_matches(['+', '-']);
    let neg = l.starts_with('-');
    let special = match body {
        "inf" | "infinity" => Some(f64::INFINITY),
        "nan" => Some(f64::NAN),
        _ => None,
    };
    if let Some(x) = special {
        return Some(if neg { -x } else { x });
    }
    if t.is_empty() || !t.chars().all(|c| c.is_ascii_digit() || "+-.eE".contains(c)) {
        return None;
    }
    t.parse().ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reprs() {
        for (x, s) in [
            (1e16, "1e+16"),
            (1e-5, "1e-05"),
            (123456789012345680.0, "1.2345678901234568e+17"),
            (0.0001, "0.0001"),
            (1.5e-7, "1.5e-07"),
            (7400.0, "7400.0"),
            (0.3, "0.3"),
            (-2.5, "-2.5"),
            (1e15, "1000000000000000.0"),
            (1.5e300, "1.5e+300"),
        ] {
            assert_eq!(float_repr(x), s);
        }
    }

    #[test]
    fn f32_texts() {
        assert_eq!(f32_text(0.3f32 as f64, false).unwrap(), "0.3");
        assert_eq!(f32_text(7400.0, false).unwrap(), "7400");
        assert_eq!(f32_text(0.0, false).unwrap(), "0");
        assert_eq!(f32_text(-0.0, false).unwrap(), "-0.0");
        assert_eq!(f32_text(1e-10f32 as f64, false).unwrap(), "1e-10");
        assert!(f32_text(f64::NAN, false).unwrap_err().msg.starts_with("nan"));
        assert_eq!(dumps(&float_value(0.1f32 as f64, false).unwrap()), "0.1");
        assert_eq!(dumps(&float_value(7400.0, false).unwrap()), "7400");
    }

    #[test]
    fn dumps_like_python() {
        let v: Value = serde_json::from_str(r#"{"a": [1, 2.5, "é\u007f"], "b": {}, "c": []}"#).unwrap();
        assert_eq!(dumps(&v), r#"{"a": [1, 2.5, "\u00e9\u007f"], "b": {}, "c": []}"#);
        assert_eq!(dumps_indent(&v, 1, true), "{\n \"a\": [\n  1,\n  2.5,\n  \"\\u00e9\\u007f\"\n ],\n \"b\": {},\n \"c\": []\n}");
        assert!(eq(&serde_json::json!([1, true]), &serde_json::json!([1.0, 1])));
        assert_eq!(repr(&serde_json::json!(["a'b", null, 1.0])), r#"["a'b", None, 1.0]"#);
    }
}
