//! Saving edited JSONC without losing what the person wrote: the file is parsed into spans
//! (where each value, member and comma is), the old and new values are compared, and only
//! what changed is rewritten in place: a changed number or string, a member added (before the
//! closing brace, indented as its siblings) or removed (with its comma), an array whose length
//! changed (written again). Comments, blank lines, key order and formatting elsewhere stay.

use serde_json::Value;

#[derive(Debug, Clone)]
enum Kind {
    Obj(Vec<Member>),
    Arr(Vec<Node>),
    Scalar,
}

#[derive(Debug, Clone)]
struct Node {
    start: usize,
    end: usize,
    kind: Kind,
}

#[derive(Debug, Clone)]
struct Member {
    key: String,
    /// from the key's quote to the value's end
    start: usize,
    value: Node,
}

struct P<'a> {
    s: &'a [u8],
    i: usize,
}

impl<'a> P<'a> {
    fn trivia(&mut self) {
        loop {
            while self.i < self.s.len() && (self.s[self.i] as char).is_whitespace() {
                self.i += 1;
            }
            if self.s[self.i..].starts_with(b"//") {
                while self.i < self.s.len() && self.s[self.i] != b'\n' {
                    self.i += 1;
                }
            } else if self.s[self.i..].starts_with(b"/*") {
                self.i += 2;
                while self.i + 1 < self.s.len() && !self.s[self.i..].starts_with(b"*/") {
                    self.i += 1;
                }
                self.i = (self.i + 2).min(self.s.len());
            } else {
                return;
            }
        }
    }

    fn string(&mut self) -> Result<String, String> {
        let start = self.i;
        self.i += 1;
        while self.i < self.s.len() && self.s[self.i] != b'"' {
            if self.s[self.i] == b'\\' {
                self.i += 1;
            }
            self.i += 1;
        }
        if self.i >= self.s.len() {
            return Err(format!("a string at {start} doesn't end"));
        }
        self.i += 1;
        serde_json::from_slice::<String>(&self.s[start..self.i]).map_err(|e| e.to_string())
    }

    fn value(&mut self) -> Result<Node, String> {
        self.trivia();
        let start = self.i;
        match self.s.get(self.i) {
            Some(b'{') => {
                self.i += 1;
                let mut members = Vec::new();
                loop {
                    self.trivia();
                    match self.s.get(self.i) {
                        Some(b'}') => {
                            self.i += 1;
                            break;
                        }
                        Some(b',') => self.i += 1,
                        Some(b'"') => {
                            let ms = self.i;
                            let key = self.string()?;
                            self.trivia();
                            if self.s.get(self.i) != Some(&b':') {
                                return Err(format!("a ':' after the key {key:?}"));
                            }
                            self.i += 1;
                            let value = self.value()?;
                            members.push(Member { key, start: ms, value });
                        }
                        _ => return Err(format!("an object member at {}", self.i)),
                    }
                }
                Ok(Node { start, end: self.i, kind: Kind::Obj(members) })
            }
            Some(b'[') => {
                self.i += 1;
                let mut items = Vec::new();
                loop {
                    self.trivia();
                    match self.s.get(self.i) {
                        Some(b']') => {
                            self.i += 1;
                            break;
                        }
                        Some(b',') => self.i += 1,
                        Some(_) => items.push(self.value()?),
                        None => return Err("an array doesn't end".into()),
                    }
                }
                Ok(Node { start, end: self.i, kind: Kind::Arr(items) })
            }
            Some(b'"') => {
                self.string()?;
                Ok(Node { start, end: self.i, kind: Kind::Scalar })
            }
            Some(_) => {
                while self.i < self.s.len() && !matches!(self.s[self.i], b',' | b'}' | b']' | b'/') && !(self.s[self.i] as char).is_whitespace() {
                    self.i += 1;
                }
                if self.i == start {
                    return Err(format!("a value at {start}"));
                }
                Ok(Node { start, end: self.i, kind: Kind::Scalar })
            }
            None => Err("the file ends early".into()),
        }
    }
}

/// One line, short values; longer ones over lines at `indent` (spaces) for nested levels.
pub fn format_value(v: &Value, indent: usize) -> String {
    let one = inline(v);
    if one.len() <= 100 {
        return one;
    }
    let pad = " ".repeat(indent + 2);
    let close = " ".repeat(indent);
    match v {
        Value::Object(m) => {
            let parts: Vec<String> = m.iter().map(|(k, x)| format!("{pad}{}: {}", inline(&Value::String(k.clone())), format_value(x, indent + 2))).collect();
            format!("{{\n{}\n{close}}}", parts.join(",\n"))
        }
        Value::Array(a) => {
            let parts: Vec<String> = a.iter().map(|x| format!("{pad}{}", format_value(x, indent + 2))).collect();
            format!("[\n{}\n{close}]", parts.join(",\n"))
        }
        _ => one,
    }
}

fn inline(v: &Value) -> String {
    match v {
        Value::Object(m) if m.is_empty() => "{}".into(),
        Value::Object(m) => format!(
            "{{ {} }}",
            m.iter().map(|(k, x)| format!("{}: {}", serde_json::to_string(k).unwrap(), inline(x))).collect::<Vec<_>>().join(", ")
        ),
        Value::Array(a) => format!("[{}]", a.iter().map(inline).collect::<Vec<_>>().join(", ")),
        _ => serde_json::to_string(v).unwrap(),
    }
}

fn line_indent(s: &str, at: usize) -> usize {
    let line = s[..at].rfind('\n').map(|i| i + 1).unwrap_or(0);
    s[line..].bytes().take_while(|b| *b == b' ').count()
}

fn diff(s: &str, n: &Node, old: &Value, new: &Value, edits: &mut Vec<(usize, usize, String)>) {
    if old == new {
        return;
    }
    match (&n.kind, old, new) {
        (Kind::Obj(members), Value::Object(o), Value::Object(w)) => {
            // changed and removed members
            for (i, m) in members.iter().enumerate() {
                match (o.get(&m.key), w.get(&m.key)) {
                    (Some(a), Some(b)) => diff(s, &m.value, a, b, edits),
                    (Some(_), None) => {
                        // the member and the comma after it (or the one before, for the last)
                        let bytes = s.as_bytes();
                        let mut end = m.value.end;
                        let mut j = end;
                        while j < bytes.len() && (bytes[j] == b' ' || bytes[j] == b'\t') {
                            j += 1;
                        }
                        let mut start = m.start;
                        if j < bytes.len() && bytes[j] == b',' {
                            end = j + 1;
                        } else if i > 0 {
                            // the previous member's comma
                            let prev = &members[i - 1];
                            if let Some(c) = s[prev.value.end..m.start].find(',') {
                                start = prev.value.end + c;
                            }
                        }
                        // the whole line when the member was alone on it
                        let ls = s[..start].rfind('\n').map(|x| x + 1).unwrap_or(0);
                        let le = s[end..].find('\n').map(|x| end + x).unwrap_or(s.len());
                        if s[ls..start].trim().is_empty() && s[end..le].trim().is_empty() {
                            edits.push((ls, (le + 1).min(s.len()), String::new()));
                        } else {
                            edits.push((start, end, String::new()));
                        }
                    }
                    _ => {}
                }
            }
            // added members: before the closing brace
            let added: Vec<(&String, &Value)> = w.iter().filter(|(k, _)| !o.contains_key(*k)).collect();
            if !added.is_empty() {
                let close = n.end - 1;
                let base = line_indent(s, n.start);
                let ind = members.first().map(|m| line_indent(s, m.start)).unwrap_or(base + 2);
                let multi = s[n.start..n.end].contains('\n') || members.is_empty();
                let mut text = String::new();
                let last_end = members.last().map(|m| m.value.end);
                let has_trailing = last_end.map(|e| s[e..close].trim_start().starts_with(',')).unwrap_or(false);
                for (k, v) in added {
                    let item = format!("{}: {}", serde_json::to_string(k).unwrap(), format_value(v, ind));
                    if multi {
                        text.push_str(&format!("\n{}{item},", " ".repeat(ind)));
                    } else {
                        text.push_str(&format!(" {item},"));
                    }
                }
                match last_end {
                    Some(e) => {
                        // after the last member (its comma, if it has one)
                        let at = if has_trailing { e + s[e..close].find(',').unwrap() + 1 } else { e };
                        let t = if has_trailing { text.trim_end_matches(',').to_string() } else { format!(",{}", text.trim_end_matches(',')) };
                        edits.push((at, at, t));
                    }
                    None => {
                        let t = format!("{}\n{}", text.trim_end_matches(','), " ".repeat(base));
                        edits.push((n.start + 1, close, t));
                    }
                }
            }
        }
        (Kind::Arr(items), Value::Array(o), Value::Array(w)) if o.len() == w.len() && items.len() == o.len() => {
            for ((it, a), b) in items.iter().zip(o).zip(w) {
                diff(s, it, a, b, edits);
            }
        }
        _ => edits.push((n.start, n.end, format_value(new, line_indent(s, n.start)))),
    }
}

/// The file `text` (JSONC whose value is `old`) with `new` in it, changed in place.
pub fn rewrite(text: &str, old: &Value, new: &Value) -> Result<String, String> {
    let mut p = P { s: text.as_bytes(), i: 0 };
    let root = p.value()?;
    let mut edits = Vec::new();
    diff(text, &root, old, new, &mut edits);
    edits.sort_by(|a, b| b.0.cmp(&a.0).then(b.1.cmp(&a.1)));
    let mut out = text.to_string();
    for (s, e, t) in edits {
        out.replace_range(s..e, &t);
    }
    Ok(out)
}

/// A new file's text (no old one to keep): pretty, two spaces.
pub fn new_text(v: &Value, header: &str) -> String {
    let mut s = String::new();
    for l in header.lines() {
        s.push_str(&format!("// {l}\n"));
    }
    s.push_str(&format_value(v, 0));
    s.push('\n');
    s
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testdrive::strip_jsonc;

    fn parse(t: &str) -> Value {
        serde_json::from_str(&strip_jsonc(t)).unwrap()
    }

    fn check(t: &str, new: Value) -> String {
        let old = parse(t);
        let out = rewrite(t, &old, &new).unwrap();
        assert_eq!(parse(&out), new, "the rewrite reads back:\n{out}");
        out
    }

    #[test]
    fn keeps_comments_and_order() {
        let t = "// head\n{\n  \"b\": 1, // one\n  \"a\": { \"x\": [1, 2, 3] },\n  /* c */ \"s\": \"x\"\n}\n";
        let mut v = parse(t);
        v["b"] = 2.into();
        v["a"]["x"][1] = 5.5.into();
        let out = check(t, v);
        assert_eq!(out, "// head\n{\n  \"b\": 2, // one\n  \"a\": { \"x\": [1, 5.5, 3] },\n  /* c */ \"s\": \"x\"\n}\n");
    }

    #[test]
    fn add_and_remove() {
        let t = "{\n  \"a\": 1, // keep\n  \"b\": 2,\n  \"c\": { \"d\": 3 }\n}\n";
        let mut v = parse(t);
        v.as_object_mut().unwrap().remove("b");
        v["c"]["e"] = 4.into();
        v["z"] = serde_json::json!({"k": [1, 2]});
        let out = check(t, v);
        assert!(out.contains("// keep"), "{out}");
        assert!(!out.contains("\"b\""), "{out}");
        // the last one
        let mut v = parse(t);
        v.as_object_mut().unwrap().remove("c");
        check(t, v);
        // trailing commas
        let t2 = "{\n  \"a\": 1,\n  \"b\": 2,\n}\n";
        let mut v = parse(t2);
        v["c"] = 3.into();
        check(t2, v);
        // into an empty object
        let t3 = "{ \"o\": {} }";
        let mut v = parse(t3);
        v["o"]["n"] = 1.into();
        check(t3, v);
    }

    #[test]
    fn arrays_and_types() {
        let t = "{ \"l\": [1, 2], \"s\": \"x\", \"n\": null }";
        let mut v = parse(t);
        v["l"] = serde_json::json!([1, 2, 3]);
        v["s"] = serde_json::json!({"now": "an object"});
        v["n"] = "text with \"quotes\"".into();
        check(t, v);
    }
}
