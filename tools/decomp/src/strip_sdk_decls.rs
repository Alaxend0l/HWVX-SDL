//! hwvx-decomp strip-sdk-decls FILES...
//!
//! Remove ad-hoc SDK/libc prototypes and duplicate SDK typedefs from game files.
//!
//! The decomp declares the functions it calls locally, often with signatures that
//! differ from file to file. The port declares them once (src/port/sdk.h, gx.h and
//! the C library headers pulled in by common.h), so local copies are dropped.
//!
//!   hwvx-decomp strip-sdk-decls src/game/*.c src/game/include/*.h

use crate::util::{escape, finditer, g, re, re_big, read, sub, write};
use std::collections::BTreeSet;
use std::path::Path;

const HEADERS: [&str; 3] = ["src/port/sdk.h", "src/port/gx.h", "src/port/lgwheels.h"];

const LIBC: &str = "memset memcpy memmove memcmp strlen strcpy strncpy strcat strcmp strncmp strchr
strrchr strstr sprintf snprintf vsprintf printf tolower toupper rand srand atan atan2 sin
cos tan acos asin sqrt sqrtf pow fabs fabsf floor ceil fmod time exit free malloc abs
atoi";

const KEYWORDS: &str =
    "void char short int long float double signed unsigned const struct union enum typedef return else case goto do sizeof if while for switch";

const SDK_TYPES: [&str; 4] = ["GXTexObj", "GXColor", "GXColorS10", "GXLightObj"];

fn sdk_names(root: &Path) -> BTreeSet<String> {
    let r = re(r"(?m)^[ \t]*(?:const[ \t]+)?(?:struct[ \t]+)?\w+[ \t\*]+([A-Za-z_]\w*)[ \t]*\(");
    let mut names = BTreeSet::new();
    for h in HEADERS {
        for m in finditer(&r, &read(root.join(h))) {
            names.insert(g(&m, 1).to_string());
        }
    }
    for k in KEYWORDS.split_whitespace() {
        names.remove(k);
    }
    names.extend(LIBC.split_whitespace().map(String::from));
    names
}

pub fn run(root: &Path, args: Vec<String>) -> i32 {
    let names = sdk_names(root);
    // Python joins the names unescaped; they are identifiers.
    let alt = names.iter().map(|s| s.as_str()).collect::<Vec<_>>().join("|");
    let decl = re_big(&format!(
        r"(?m)^[ \t]*(?:extern[ \t]+)?(?:static[ \t]+)?(?!(?:return|else|case|goto|do|typedef)\b)(?:const[ \t]+)?(?:struct[ \t]+)?\w+[\w \t\*]*?\b({alt})[ \t]*\([^;{{}}=()]*(?:\([^;{{}}=()]*\)[^;{{}}=()]*)*\)[ \t]*;[ \t]*(?://.*)?\n"
    ));
    let tdef = re(&format!(
        r"(?m)^typedef struct ({})\s*\{{[^}}]*\}}\s*\1\s*;\n",
        SDK_TYPES.iter().map(|s| escape(s)).collect::<Vec<_>>().join("|")
    ));
    for path in &args {
        let p = Path::new(path);
        let src = read(p);
        let out = sub(&decl, &src, |_| String::new());
        let out = sub(&tdef, &out, |_| String::new());
        if out != src {
            write(p, &out);
            println!(
                "{}: -{} lines",
                p.file_name().unwrap().to_string_lossy(),
                src.matches('\n').count() as i64 - out.matches('\n').count() as i64
            );
        }
    }
    0
}
