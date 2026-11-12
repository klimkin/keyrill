//! Require explicit baseline reconciliation and actually executed Rust successors.

use serde_json::Value;
use std::collections::BTreeSet;
use std::error::Error;
use std::fs;
use std::path::Path;

type Result<T> = std::result::Result<T, Box<dyn Error>>;

fn validate(ledger: &Value, executed: &BTreeSet<String>) -> Result<()> {
    if ledger["schema"] != 1
        || ledger["baselineCommit"] != "03e58ca542445855943eb6ac74e10a52c4ab5633"
    {
        return Err("unsupported Windows baseline coverage ledger".into());
    }
    let families = ledger["families"]
        .as_array()
        .ok_or("baseline family inventory missing")?;
    if families.is_empty() || executed.is_empty() {
        return Err("baseline coverage requires inventory and executed Rust tests".into());
    }
    let mut seen = BTreeSet::new();
    for family in families {
        let id = family["id"]
            .as_str()
            .ok_or("baseline family identity missing")?;
        if !seen.insert(id) {
            return Err("duplicate baseline family identity".into());
        }
        let tests = family["rustTests"]
            .as_array()
            .ok_or("Rust successor list missing")?;
        match family["disposition"].as_str() {
            Some("ported") => {
                if tests.is_empty() {
                    return Err(format!("ported family has no Rust successor {id}").into());
                }
            }
            Some("helper" | "retired-contract") => {
                if family["reconciliations"].as_array().is_none_or(|notes| {
                    notes.is_empty()
                        || notes
                            .iter()
                            .any(|note| note.as_str().is_none_or(str::is_empty))
                }) {
                    return Err(format!("baseline reconciliation explanation missing {id}").into());
                }
            }
            _ => return Err(format!("baseline family remains unclassified {id}").into()),
        }
        for test in tests {
            let test = test.as_str().ok_or("Rust successor identity invalid")?;
            if !executed.contains(test) {
                return Err(format!("baseline successor was not executed {id} -> {test}").into());
            }
        }
    }
    Ok(())
}

pub(crate) fn check(root: &Path, executed: &BTreeSet<String>) -> Result<()> {
    let path = root.join("native/windows/proof/scenarios.json");
    let ledger = serde_json::from_slice(&fs::read(path)?)?;
    validate(&ledger, executed)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn ledger() -> Value {
        json!({"schema": 1, "baselineCommit": "03e58ca542445855943eb6ac74e10a52c4ab5633", "families": [{"id": "original.schedule", "disposition": "ported", "rustTests": ["controlled::complete_document"], "reconciliations": []}]})
    }

    #[test]
    fn requires_executed_successors_and_rejects_unclassified_families() {
        let executed = BTreeSet::from(["controlled::complete_document".to_owned()]);
        assert!(validate(&ledger(), &executed).is_ok());
        assert!(validate(&ledger(), &BTreeSet::new()).is_err());
        assert!(validate(&ledger(), &BTreeSet::from(["other".to_owned()])).is_err());
        let mut unclassified = ledger();
        unclassified["families"][0]["disposition"] = json!("unclassified");
        assert_eq!(
            validate(&unclassified, &executed).unwrap_err().to_string(),
            "baseline family remains unclassified original.schedule"
        );
    }

    #[test]
    fn retired_contract_and_helper_records_require_a_reason() {
        let executed = BTreeSet::from(["controlled::complete_document".to_owned()]);
        for disposition in ["retired-contract", "helper"] {
            let mut changed = ledger();
            changed["families"][0]["disposition"] = json!(disposition);
            assert!(validate(&changed, &executed).is_err());
            changed["families"][0]["reconciliations"] = json!([
                "The old foreign throw crosses an ABI that now requires COM no-throw semantics. Its HRESULT and reference-reentry successors execute separately."
            ]);
            assert!(validate(&changed, &executed).is_ok());
            changed["families"][0]["reconciliations"] = json!([""]);
            assert!(validate(&changed, &executed).is_err());
        }
    }

    #[test]
    fn empty_or_duplicate_inventory_cannot_pass() {
        let executed = BTreeSet::from(["controlled::complete_document".to_owned()]);
        let mut empty = ledger();
        empty["families"] = json!([]);
        assert!(validate(&empty, &executed).is_err());
        let mut duplicate = ledger();
        let row = duplicate["families"][0].clone();
        duplicate["families"].as_array_mut().unwrap().push(row);
        assert!(validate(&duplicate, &executed).is_err());
    }
}
