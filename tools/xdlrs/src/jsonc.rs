//! JSONC: JSON with // and /* */ comments and trailing commas (a comma before } or ], outside
//! strings), as xdl_struct.jsonc_loads reads it, and the text the tools write (dump_value,
//! to_jsonc). `strip` is the browser's lenient reader (an unterminated string or comment runs
//! to the end instead of failing); `loads` is the Python's (those fail, with the offset).

use crate::error::{Error, Result};
use crate::py;
use crate::schema;
use serde_json::Value;
use std::collections::HashMap;

/// Comments out (strict: an unterminated string or comment is an error at its char offset).
fn uncomment(b: &[char], strict: bool) -> Result<String> {
    let mut out = String::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        let c = b[i];
        if c == '"' {
            let start = i;
            out.push(c);
            i += 1;
            let mut closed = false;
            while i < b.len() {
                if b[i] == '\\' {
                    if i + 1 >= b.len() {
                        out.push(b[i]);
                        i += 1;
                        break;
                    }
                    out.push(b[i]);
                    out.push(b[i + 1]);
                    i += 2;
                    continue;
                }
                out.push(b[i]);
                i += 1;
                if b[i - 1] == '"' {
                    closed = true;
                    break;
                }
            }
            if !closed && strict {
                return Err(Error::xdl(format!("unterminated string at offset {start}")));
            }
        } else if c == '/' && i + 1 < b.len() && b[i + 1] == '/' {
            while i < b.len() && b[i] != '\n' {
                i += 1;
            }
        } else if c == '/' && i + 1 < b.len() && b[i + 1] == '*' {
            let start = i;
            i += 2;
            while i + 1 < b.len() && !(b[i] == '*' && b[i + 1] == '/') {
                i += 1;
            }
            if i + 1 >= b.len() && strict {
                return Err(Error::xdl(format!("unterminated comment at offset {start}")));
            }
            i += 2;
        } else {
            out.push(c);
            i += 1;
        }
    }
    Ok(out)
}

/// Trailing commas out (strings kept as they are).
fn uncomma(b: &[char]) -> String {
    let mut out = String::with_capacity(b.len());
    let mut i = 0;
    while i < b.len() {
        let c = b[i];
        if c == '"' {
            out.push(c);
            i += 1;
            while i < b.len() {
                out.push(b[i]);
                if b[i] == '\\' && i + 1 < b.len() {
                    out.push(b[i + 1]);
                    i += 2;
                    continue;
                }
                i += 1;
                if b[i - 1] == '"' {
                    break;
                }
            }
        } else if c == ',' {
            let mut j = i + 1;
            while j < b.len() && b[j].is_whitespace() {
                j += 1;
            }
            if !(j < b.len() && (b[j] == '}' || b[j] == ']')) {
                out.push(c);
            }
            i += 1;
        } else {
            out.push(c);
            i += 1;
        }
    }
    out
}

/// JSONC to JSON, leniently (the browser's reader): comments out, then trailing commas out.
pub fn strip(s: &str) -> String {
    let b: Vec<char> = s.chars().collect();
    let t = uncomment(&b, false).unwrap();
    uncomma(&t.chars().collect::<Vec<_>>())
}

/// xdl_struct.jsonc_loads. Errors: Kind::Xdl for an unterminated string or comment,
/// Kind::Json for bad JSON (the message is serde_json's, not Python's).
pub fn loads(text: &str) -> Result<Value> {
    // fast path: the files the tools write are JSON after leading // comment lines
    let mut i = 0;
    loop {
        let rest = &text[i..];
        let j = i + (rest.len() - rest.trim_start().len());
        if !text[j..].starts_with("//") {
            break;
        }
        i = match text[j..].find('\n') {
            Some(k) => j + k + 1,
            None => text.len(),
        };
    }
    if let Ok(v) = parse(&text[i..]) {
        return Ok(v);
    }
    let b: Vec<char> = text.chars().collect();
    let s = uncomment(&b, true)?;
    let s = uncomma(&s.chars().collect::<Vec<_>>());
    parse(&s)
}

/// json.loads (without NaN/Infinity, which Python accepts): "-0" reads as the int 0.
pub fn parse(s: &str) -> Result<Value> {
    let fixed;
    let s = if s.contains("-0") {
        fixed = int_neg_zero(s);
        fixed.as_str()
    } else {
        s
    };
    serde_json::from_str(s).map_err(|e| Error::json(e.to_string()))
}

/// "-0" number tokens (outside strings) as "0": serde_json reads them as -0.0, Python as 0.
fn int_neg_zero(s: &str) -> String {
    let b = s.as_bytes();
    let mut out = String::with_capacity(s.len());
    let mut i = 0;
    let mut last = 0;
    let mut in_str = false;
    while i < b.len() {
        let c = b[i];
        if in_str {
            if c == b'\\' {
                i += 2;
                continue;
            }
            if c == b'"' {
                in_str = false;
            }
        } else if c == b'"' {
            in_str = true;
        } else if c == b'-' && b.get(i + 1) == Some(&b'0') && !b.get(i + 2).is_some_and(|d| b".eE0123456789".contains(d)) {
            out.push_str(&s[last..i]);
            last = i + 1;
        }
        i += 1;
    }
    out.push_str(&s[last.min(s.len())..]);
    out
}

/// xdl_struct.dump_value: two-space indent, short scalar lists on one line (<= 100 chars),
/// `comments` (key -> text) after a member of the top object.
pub fn dump_value(v: &Value, ind: usize, comments: Option<&HashMap<String, String>>) -> String {
    let pad = "  ".repeat(ind);
    match v {
        Value::Object(m) => {
            if m.is_empty() {
                return "{}".into();
            }
            let n = m.len();
            let lines: Vec<String> = m
                .iter()
                .enumerate()
                .map(|(k, (key, x))| {
                    let sep = if k + 1 < n { "," } else { "" };
                    let com = comments.and_then(|c| c.get(key)).filter(|c| !c.is_empty());
                    format!(
                        "{pad}  {}: {}{sep}{}",
                        py::json_str(key),
                        dump_value(x, ind + 1, None),
                        com.map(|c| format!("  // {c}")).unwrap_or_default()
                    )
                })
                .collect();
            format!("{{\n{}\n{pad}}}", lines.join("\n"))
        }
        Value::Array(a) => {
            if a.iter().all(|x| !x.is_object() && !x.is_array()) {
                let one = format!("[{}]", a.iter().map(|x| dump_value(x, ind + 1, None)).collect::<Vec<_>>().join(", "));
                if one.chars().count() <= 100 {
                    return one;
                }
            }
            let items: Vec<String> = a.iter().map(|x| format!("{pad}  {}", dump_value(x, ind + 1, None))).collect();
            format!("[\n{}\n{pad}]", items.join(",\n"))
        }
        _ => py::dumps(v),
    }
}

/// xdl_struct.to_jsonc: header lines as // comments, then the document, x-units as comments.
pub fn to_jsonc(doc: &Value, sch: &Value, header: &[String]) -> String {
    let mut comments = HashMap::new();
    for (key, p) in schema::fields(sch) {
        if let Some(u) = p.get("x-units") {
            comments.insert(key.clone(), py::str_of(u));
        }
    }
    let mut s: String = header.iter().map(|h| format!("// {h}\n")).collect();
    s.push_str(&dump_value(doc, 0, Some(&comments)));
    s.push('\n');
    s
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn loads_jsonc() {
        let v = loads("// head\n{\"a\": [1, 2,], /* c */ \"b\": \"//x\", // t\n}").unwrap();
        assert_eq!(py::dumps(&v), r#"{"a": [1, 2], "b": "//x"}"#);
        assert_eq!(loads("{\"a\": \"x}").unwrap_err().msg, "unterminated string at offset 6");
        assert_eq!(loads("{} /* x").unwrap_err().msg, "unterminated comment at offset 3");
        assert_eq!(loads("{\"a\": 1 /*").unwrap_err().kind, crate::error::Kind::Xdl);
        assert_eq!(loads("{\"a\": }").unwrap_err().kind, crate::error::Kind::Json);
        assert_eq!(strip("{\"a\": 1 /* x"), "{\"a\": 1 ");
    }

    #[test]
    fn dump() {
        let v: Value = serde_json::from_str(r#"{"a": [1, 2], "b": {"c": [{"d": null}]}, "e": {}}"#).unwrap();
        assert_eq!(
            dump_value(&v, 0, None),
            "{\n  \"a\": [1, 2],\n  \"b\": {\n    \"c\": [\n      {\n        \"d\": null\n      }\n    ]\n  },\n  \"e\": {}\n}"
        );
    }
}

#[cfg(test)]
mod zero_tests {
    #[test]
    fn neg_zero() {
        let v = super::parse("[-0, -0.0, 0]").unwrap();
        assert_eq!(crate::py::dumps(&v), "[0, -0.0, 0]");
    }
}
