//! The retail cars by number, as the car select names them (the same 33 on GameCube, PC and
//! PS2: xdlrs cars stats, docs-pending/car-stats-inventory.md). A number past them is a car a mod
//! or the prototype adds.

pub const RETAIL: [&str; 33] = [
    "Twin Mill 2", "At-A-Tude", "Overbored 454", "MS-T Suzuka", "Super Tuned", "Sooo Fast", "Flashfire", "Mega-Duty",
    "Muscle Tone", "Sho-Stopper", "Hammered Coupe", "Jaded", "Way 2 Fast", "Deora II", "Hyper Mite", "Power Pistons",
    "Evil Twin", "Surf Crate", "Rigor Motor", "Lakester", "Splittin' Image 2", "Greased Lightnin'", "Hyperliner", "Maelstrom",
    "Sol-Aire CX4", "Surfin' School Bus", "Vulture", "Arachnorod", "Krazy 8s", "Shadow Jet", "HW Prototype 12", "Power Rocket",
    "Jet Threat 3.0",
];

/// "5  Sooo Fast", or "35  (an added car)".
pub fn label(n: u32) -> String {
    match RETAIL.get(n as usize) {
        Some(name) => format!("{n}  {name}"),
        None => format!("{n}  (an added car)"),
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn labels() {
        assert_eq!(super::label(0), "0  Twin Mill 2");
        assert_eq!(super::label(32), "32  Jet Threat 3.0");
        assert_eq!(super::label(33), "33  (an added car)");
    }
}
