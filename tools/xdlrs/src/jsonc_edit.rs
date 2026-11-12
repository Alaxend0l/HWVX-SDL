//! Saving edited JSONC without losing what the person wrote: the file is parsed into spans
//! (where each value, member and comma is), the old and new values are compared, and only
//! what changed is rewritten in place: a changed number or string, a member added (after the
//! last member and its trailing comment, indented as its siblings) or removed (its span: the
//! comment lines above it, the member, its comma and its comment on the same line), an array
//! whose length changed (written again). Comments, blank lines, key order and formatting
//! elsewhere stay. Commas are the parser's (never one inside a comment), removals that touch
//! are merged, and the result is read back and compared with what was asked for: a rewrite
//! that wouldn't read back as `new` is an error, never a file.

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
    /// the comma that follows it (the parser's: never one in a comment)
    comma: Option<usize>,
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
                let mut members: Vec<Member> = Vec::new();
                loop {
                    self.trivia();
                    match self.s.get(self.i) {
                        Some(b'}') => {
                            self.i += 1;
                            break;
                        }
                        Some(b',') => {
                            if let Some(m) = members.last_mut() {
                                if m.comma.is_none() {
                                    m.comma = Some(self.i);
                                }
                            }
                            self.i += 1
                        }
                        Some(b'"') => {
                            let ms = self.i;
                            let key = self.string()?;
                            self.trivia();
                            if self.s.get(self.i) != Some(&b':') {
                                return Err(format!("a ':' after the key {key:?}"));
                            }
                            self.i += 1;
                            let value = self.value()?;
                            members.push(Member { key, start: ms, value, comma: None });
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
        (Kind::Obj(members), Value::Object(o), Value::Object(w)) => object(s, n, members, o, w, edits),
        (Kind::Arr(items), Value::Array(o), Value::Array(w)) if o.len() == w.len() && items.len() == o.len() => {
            for ((it, a), b) in items.iter().zip(o).zip(w) {
                diff(s, it, a, b, edits);
            }
        }
        _ => edits.push((n.start, n.end, format_value(new, line_indent(s, n.start)))),
    }
}

/// Where member i's span ends: its value, its comma (when right after it), and the rest of its
/// line when that is only spaces and a comment (the newline included).
fn span_end(s: &str, m: &Member) -> usize {
    let b = s.as_bytes();
    let mut j = m.value.end;
    let skip = |j: &mut usize| {
        while *j < b.len() && (b[*j] == b' ' || b[*j] == b'\t' || b[*j] == b'\r') {
            *j += 1;
        }
    };
    skip(&mut j);
    if m.comma == Some(j) {
        j += 1;
        skip(&mut j);
    }
    let k = j;
    if s[j..].starts_with("//") {
        j += s[j..].find('\n').unwrap_or(s.len() - j);
    } else if s[j..].starts_with("/*") {
        match s[j..].find("*/") {
            Some(e) if !s[j..j + e].contains('\n') => {
                j += e + 2;
                skip(&mut j);
            }
            _ => return k,
        }
    }
    if j < b.len() && b[j] == b'\n' {
        j + 1
    } else if s[k..j].is_empty() {
        k
    } else {
        j
    }
}

/// An object's members changed, removed and added (see the top).
fn object(s: &str, n: &Node, members: &[Member], o: &serde_json::Map<String, Value>, w: &serde_json::Map<String, Value>,
          edits: &mut Vec<(usize, usize, String)>) {
    let open = n.start;
    let close = n.end - 1;
    let b = s.as_bytes();
    let multi = s[open..n.end].contains('\n');
    let base = line_indent(s, open);
    let ind = members.first().map(|m| line_indent(s, m.start)).filter(|&i| i > base).unwrap_or(base + 2);
    let item = |k: &String, v: &Value| format!("{}: {}", serde_json::to_string(k).unwrap(), format_value(v, ind));
    let removed: Vec<bool> = members.iter().map(|m| o.contains_key(&m.key) && !w.contains_key(&m.key)).collect();
    let kept: Vec<usize> = (0..members.len()).filter(|&i| !removed[i]).collect();
    let added: Vec<(&String, &Value)> = w.iter().filter(|(k, _)| !o.contains_key(*k)).collect();

    if kept.is_empty() {
        // every member goes, or the object was empty: its body written again
        if members.is_empty() && added.is_empty() {
            return;
        }
        let parts: Vec<String> = added.iter().map(|(k, v)| item(k, v)).collect();
        let body = if parts.is_empty() {
            String::new()
        } else if multi || parts.iter().any(|p| p.contains('\n')) {
            let lines: Vec<String> = parts.iter().map(|p| format!("{}{p}", " ".repeat(ind))).collect();
            format!("\n{}\n{}", lines.join(",\n"), " ".repeat(base))
        } else {
            format!(" {} ", parts.join(", "))
        };
        edits.push((open + 1, close, body));
        return;
    }
    // the members' spans: back to back, the first from the line after the brace
    let mut at = open + 1;
    {
        let mut j = at;
        while j < close && (b[j] == b' ' || b[j] == b'\t' || b[j] == b'\r') {
            j += 1;
        }
        if j < close && b[j] == b'\n' {
            at = j + 1;
        }
    }
    let mut spans = Vec::new();
    for m in members {
        let e = span_end(s, m).max(m.value.end);
        spans.push((at.min(m.start), e));
        at = e;
    }
    let in_span = |c: usize, i: usize| c >= spans[i].0 && c < spans[i].1;
    // changed members
    for &i in &kept {
        let m = &members[i];
        if let (Some(x), Some(y)) = (o.get(&m.key), w.get(&m.key)) {
            diff(s, &m.value, x, y, edits);
        }
    }
    // removed ones: their spans (the comment lines above, the member, a comma and comment on its line)
    for i in 0..members.len() {
        if removed[i] {
            edits.push((spans[i].0, spans[i].1, String::new()));
        }
    }
    // commas as separators: between two members that stay, one that survives (the first's in its
    // own span, or one first on the second's line); every other comma not in a removed span goes
    let lk = *kept.last().unwrap();
    let was_last = members.len() - 1;
    let trailing_style = members[was_last].comma.is_some();
    let mut keep = std::collections::HashSet::new();
    for pair in kept.windows(2) {
        let (a, bb) = (pair[0], pair[1]);
        if let Some(c) = members[a].comma.filter(|&c| in_span(c, a)) {
            keep.insert(c);
        } else if let Some(c) = members[bb - 1].comma.filter(|&c| in_span(c, bb)) {
            keep.insert(c);
        } else {
            let v = members[a].value.end;
            edits.push((v, v, ",".into()));
        }
    }
    if trailing_style || !added.is_empty() {
        if let Some(c) = members[lk].comma.filter(|&c| in_span(c, lk)) {
            keep.insert(c);
        }
    }
    for m in members {
        if let Some(c) = m.comma {
            if !keep.contains(&c) && !(0..members.len()).any(|i| removed[i] && in_span(c, i)) {
                edits.push((c, c + 1, String::new()));
            }
        }
    }
    if added.is_empty() {
        return;
    }
    let end = spans[lk].1;
    let parts: Vec<String> = added.iter().map(|(k, v)| item(k, v)).collect();
    let tail = if trailing_style { "," } else { "" };
    let mut text = if multi && s[..end].ends_with('\n') {
        let lines: Vec<String> = parts.iter().map(|p| format!("{}{p}", " ".repeat(ind))).collect();
        format!("{}{tail}\n", lines.join(",\n"))
    } else if multi {
        let lines: Vec<String> = parts.iter().map(|p| format!("{}{p}", " ".repeat(ind))).collect();
        format!("\n{}{tail}", lines.join(",\n"))
    } else {
        let sep = if s[..end].ends_with(' ') { "" } else { " " };
        let after = if b.get(end) == Some(&b' ') { "" } else { " " };
        format!("{sep}{}{tail}{after}", parts.join(", "))
    };
    if !members[lk].comma.map(|c| keep.contains(&c)).unwrap_or(false) {
        let v = members[lk].value.end;
        if v == end {
            text.insert(0, ',');
        } else {
            edits.push((v, v, ",".into()));
        }
    }
    edits.push((end, end, text));
}

/// The file `text` (JSONC whose value is `old`) with `new` in it, changed in place. An error
/// when the result wouldn't read back as `new` (nothing is written then).
pub fn rewrite(text: &str, old: &Value, new: &Value) -> Result<String, String> {
    let mut p = P { s: text.as_bytes(), i: 0 };
    let root = p.value()?;
    let mut edits = Vec::new();
    diff(text, &root, old, new, &mut edits);
    // deletions that overlap: one; any other overlap is a mistake (refused)
    edits.sort_by(|a, b| a.0.cmp(&b.0).then(a.1.cmp(&b.1)));
    let mut merged: Vec<(usize, usize, String)> = Vec::new();
    for e in edits {
        if let Some(l) = merged.last_mut() {
            if e.0 < l.1 {
                if e.2.is_empty() && l.2.is_empty() {
                    l.1 = l.1.max(e.1);
                    continue;
                }
                return Err(format!("the edits at {}..{} and {}..{} overlap: not saved", l.0, l.1, e.0, e.1));
            }
        }
        merged.push(e);
    }
    // back to front; at one place, the deletion first, then what is inserted there
    merged.sort_by(|a, b| b.0.cmp(&a.0).then(b.1.cmp(&a.1)));
    let mut out = text.to_string();
    for (s, e, t) in &merged {
        out.replace_range(*s..*e, t);
    }
    match serde_json::from_str::<Value>(&crate::jsonc::strip(&out)) {
        Ok(v) if &v == new => Ok(out),
        Ok(_) => Err("the rewritten file wouldn't read back as the level: not saved".into()),
        Err(e) => Err(format!("the rewritten file wouldn't parse ({e}): not saved")),
    }
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
    use crate::jsonc::strip as strip_jsonc;

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
    /// A small deterministic generator (xorshift), no crates.
    struct Rng(u64);
    impl Rng {
        fn next(&mut self) -> u64 {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 7;
            self.0 ^= self.0 << 17;
            self.0
        }
        fn below(&mut self, n: u64) -> u64 {
            self.next() % n
        }
        fn chance(&mut self, pct: u64) -> bool {
            self.below(100) < pct
        }
    }

    /// An object's text in one of four layouts (one member a line; with a trailing comma; commas
    /// first on the line; all on one line), members' comments tagged with their keys (l_KEY on the
    /// line above, c_KEY after it on its line), and its value.
    fn gen_obj(r: &mut Rng, depth: usize, indent: usize, keys: &mut u32) -> (String, Value) {
        let style = r.below(4);
        let n = r.below(5) as usize;
        let mut v = serde_json::Map::new();
        let pad = " ".repeat(indent + 2);
        let mut lines = Vec::new();
        for i in 0..n {
            *keys += 1;
            let k = format!("k{keys}");
            let (vt, vv) = match r.below(5) {
                0 if depth < 2 => gen_obj(r, depth + 1, indent + 2, keys),
                1 => ("[1, 2]".to_string(), serde_json::json!([1, 2])),
                2 => (format!("\"s{}\"", r.below(9)), Value::String(format!("s{}", r.below(1)))),
                _ => {
                    let x = r.below(100);
                    (x.to_string(), Value::from(x))
                }
            };
            let vv = if let Value::String(_) = vv { serde_json::from_str::<Value>(&vt).unwrap() } else { vv };
            v.insert(k.clone(), vv);
            let last = i + 1 == n;
            let line = match style {
                0 | 1 => {
                    let comma = if !last || style == 1 { "," } else { "" };
                    let above = if r.chance(30) { format!("{pad}// l_{k}\n") } else { String::new() };
                    let after = if r.chance(40) { format!(" // c_{k}") } else if r.chance(15) { format!(" /* c_{k} */") } else { String::new() };
                    format!("{above}{pad}\"{k}\": {vt}{comma}{after}\n")
                }
                2 => {
                    let lead = if i == 0 { "  " } else { ", " };
                    let after = if r.chance(40) { format!(" // c_{k}") } else { String::new() };
                    format!("{}{lead}\"{k}\": {vt}{after}\n", " ".repeat(indent))
                }
                _ => format!("\"{k}\": {vt}"),
            };
            lines.push(line);
        }
        let text = if style == 3 {
            if n == 0 { "{}".to_string() } else { format!("{{ {} }}", lines.join(", ")) }
        } else {
            format!("{{\n{}{}}}", lines.concat(), " ".repeat(indent))
        };
        (text, Value::Object(v))
    }

    /// Random deletions, additions and changes; the keys whose members stay (their comments must).
    fn mutate(r: &mut Rng, v: &mut Value, keys: &mut u32, kept: &mut Vec<String>) {
        let Value::Object(m) = v else { return };
        let all = r.chance(10);
        let names: Vec<String> = m.keys().cloned().collect();
        for k in names {
            if all || r.chance(35) {
                m.remove(&k);
                continue;
            }
            kept.push(k.clone());
            let x = m.get_mut(&k).unwrap();
            match x {
                Value::Object(_) => mutate(r, x, keys, kept),
                Value::Number(_) if r.chance(30) => *x = Value::from(r.below(1000) + 1000),
                Value::Array(a) if r.chance(30) => a.push(Value::from(3)),
                _ => {}
            }
        }
        for _ in 0..r.below(3) {
            *keys += 1;
            let nv = if r.chance(30) { serde_json::json!({"x": 1, "y": [1, 2]}) } else { Value::from(r.below(50)) };
            m.insert(format!("n{keys}"), nv);
        }
    }

    #[test]
    fn random_edits_read_back() {
        let mut r = Rng(std::env::var("JSONC_SEED").ok().and_then(|s| s.parse().ok()).unwrap_or(0x2545F4914F6CDD1D));
        let mut fails = Vec::new();
        let runs = std::env::var("JSONC_RUNS").ok().and_then(|s| s.parse().ok()).unwrap_or(20000);
        for run in 0..runs {
            let mut keys = 0u32;
            let (text, old) = gen_obj(&mut r, 0, 0, &mut keys);
            let text = format!("// head\n{text}\n");
            assert_eq!(parse(&text), old, "the generator: {text}");
            let mut new = old.clone();
            let mut kept = Vec::new();
            mutate(&mut r, &mut new, &mut keys, &mut kept);
            match rewrite(&text, &old, &new) {
                Ok(out) => {
                    let back: Option<Value> = serde_json::from_str(&strip_jsonc(&out)).ok();
                    if back.as_ref() != Some(&new) {
                        fails.push(format!("run {run}: reads back wrong\n{text}\n->\n{out}"));
                    } else if let Some(k) = kept.iter().find(|k| {
                        (text.contains(&format!("c_{k} ")) || text.contains(&format!("c_{k}\n")) || text.contains(&format!("l_{k}\n")))
                            && !(out.contains(&format!("c_{k}")) || out.contains(&format!("l_{k}")))
                    }) {
                        fails.push(format!("run {run}: the comment of {k} went\n{text}\n->\n{out}"));
                    }
                }
                Err(e) => fails.push(format!("run {run}: {e}\n{text}")),
            }
        }
        for f in fails.iter().take(3) {
            eprintln!("{f}\n----");
        }
        assert_eq!(fails.len(), 0, "{} of {runs} random edits failed", fails.len());
    }

    #[test]
    fn the_review_cases() {
        // the last two members deleted; the only member deleted and one added; every member
        let t = "{\n  \"a\": 1,\n  \"b\": 2, // two\n  \"c\": 3\n}\n";
        let mut v = parse(t);
        v.as_object_mut().unwrap().remove("b");
        v.as_object_mut().unwrap().remove("c");
        assert_eq!(check(t, v), "{\n  \"a\": 1\n}\n");
        let t1 = "{\n  \"only\": 1\n}\n";
        check(t1, serde_json::json!({"new": 2}));
        let t2 = "// head\n{\n  \"a\": 1,\n  \"b\": { \"x\": 2 }\n}\n";
        assert_eq!(check(t2, serde_json::json!({})), "// head\n{}\n");
        // a comma inside a comment isn't the member's
        let t3 = "{\n  \"a\": 1 // x, y\n  ,\"b\": 2\n}\n";
        let mut v = parse(t3);
        v.as_object_mut().unwrap().remove("b");
        assert!(check(t3, v).contains("// x, y"));
        // deleting the last member keeps the previous one's comment; an added member goes after
        // a trailing comment, not between the value and it
        let t4 = "{\n  \"a\": 1, // keep me\n  \"b\": 2\n}\n";
        let mut v = parse(t4);
        v.as_object_mut().unwrap().remove("b");
        let out = check(t4, v);
        assert!(out.contains("// keep me"), "{out}");
        let t5 = "{\n  \"a\": 1 // after a\n}\n";
        let mut v = parse(t5);
        v["z"] = 2.into();
        assert_eq!(check(t5, v), "{\n  \"a\": 1, // after a\n  \"z\": 2\n}\n");
    }
}

