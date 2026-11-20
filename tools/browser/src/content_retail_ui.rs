//! The Content tab's Retail changes: the game's own cars and weapons changed by the mod (its
//! top-level patches and recipes, src/content_retail.rs), in the same inspector as a new entry's
//! (content_ui's numbers and paint, through a Target), and tried in the game with the mod on.

use crate::Browser;
use crate::content_ui::base_label;
use eframe::egui;
use hwvx_browser::content::Kind;
use hwvx_browser::content_retail::{Target, retail_pack};

#[derive(Default)]
pub(crate) struct RetailState {
    /// the retail car or weapon on show (a new entry's selection clears it, and it clears that)
    pub sel: Option<(Kind, u32)>,
    /// the mod the first retail change was selected for (on opening a mod with no entry selected)
    pub opened: Option<std::path::PathBuf>,
}

impl Browser {
    /// The left panel's list: every retail car and weapon, the changed ones marked.
    pub(crate) fn content_retail_panel(&mut self, ui: &mut egui::Ui) {
        if self.content.sel.is_some() {
            self.content.retail.sel = None;
        }
        let Some(p) = self.content.proj.as_ref() else { return };
        let changes = |kind: Kind| (0..kind.retail_count()).map(|n| p.retail_changes(kind, n)).collect::<Vec<_>>();
        let (cars, weapons) = (changes(Kind::Car), changes(Kind::Weapon));
        let changed = cars.iter().chain(&weapons).filter(|&&c| c > 0).count();
        if self.content.retail.opened.as_ref() != Some(&p.dir) {
            self.content.retail.opened = Some(p.dir.clone());
            if self.content.sel.is_none() {
                let first = |kind: Kind, v: &[usize]| v.iter().position(|&c| c > 0).map(|n| (kind, n as u32));
                self.content.retail.sel = first(Kind::Car, &cars).or_else(|| first(Kind::Weapon, &weapons));
            }
        }
        ui.separator();
        let title = if changed > 0 { format!("Retail changes ({changed})") } else { "Retail changes".to_string() };
        egui::CollapsingHeader::new(title).id_salt("content_retail").default_open(changed > 0).show(ui, |ui| {
            ui.label(egui::RichText::new("The game's own cars and weapons, changed (numbers, paint) wherever the game uses them.").small().weak());
            for (kind, counts) in [(Kind::Car, cars), (Kind::Weapon, weapons)] {
                ui.strong(if kind == Kind::Car { "Cars" } else { "Weapons (the item select's)" });
                egui::ScrollArea::vertical().id_salt(("content_retail_list", kind == Kind::Car)).max_height(if kind == Kind::Car { 180.0 } else { 120.0 }).show(ui, |ui| {
                    for (n, c) in counts.into_iter().enumerate() {
                        let n = n as u32;
                        let selected = self.content.retail.sel == Some((kind, n));
                        let label = if c > 0 {
                            egui::RichText::new(format!("● {}   ({c} changed)", base_label(kind, n))).color(egui::Color32::from_rgb(230, 200, 90))
                        } else {
                            egui::RichText::new(base_label(kind, n))
                        };
                        if ui.selectable_label(selected, label).on_hover_text(retail_pack(kind, n)).clicked() {
                            self.content.retail.sel = Some((kind, n));
                            self.content.sel = None;
                        }
                    }
                });
            }
        });
    }

    /// The right panel for a retail car or weapon; false when none is selected.
    pub(crate) fn content_retail_inspector(&mut self, ui: &mut egui::Ui) -> bool {
        let Some((kind, n)) = self.content.retail.sel else { return false };
        if self.content.sel.is_some() || self.content.proj.is_none() {
            self.content.retail.sel = None;
            return false;
        }
        let changes = self.content.proj.as_ref().map(|p| p.retail_changes(kind, n)).unwrap_or(0);
        ui.heading(if kind == Kind::Car { "Retail car" } else { "Retail weapon" });
        ui.strong(base_label(kind, n));
        ui.label(egui::RichText::new(retail_pack(kind, n)).small().weak());
        ui.label(egui::RichText::new(if kind == Kind::Car {
            "The game's own car, changed wherever it races (the AI's too). The mod's top-level patches and recipes."
        } else {
            "The game's own weapon, changed wherever it is used. New weapons made from it keep the game's numbers. The mod's top-level patches."
        }).weak());
        ui.horizontal(|ui| {
            let busy = self.test_drive_busy();
            let b = egui::Button::new(egui::RichText::new(if kind == Kind::Car { "▶ Drive it" } else { "▶ Try it" }).strong().color(egui::Color32::WHITE)).fill(egui::Color32::from_rgb(30, 120, 60));
            if ui.add_enabled(!busy, b).on_hover_text(if kind == Kind::Car { "a Challenge race in this car with the mod on (saved first)" } else { "a Battle with this weapon with the mod on (saved first)" }).clicked() {
                self.content_test(kind, &n.to_string());
            }
            if ui.add_enabled(changes > 0, egui::Button::new("↺ Reset")).on_hover_text("drop the mod's changes to it: the game's own again").clicked() {
                if let Some(p) = self.content.proj.as_mut() {
                    p.retail_reset(kind, n);
                }
            }
        });
        ui.separator();
        let target = Target::Retail(kind, n);
        egui::ScrollArea::vertical().show(ui, |ui| {
            if kind == Kind::Car {
                self.content_paint(ui, &target);
                ui.separator();
            }
            self.content_numbers(ui, &target);
        });
        true
    }
}
