//! Host keystroke routing and its canonical status row.
/// Canonical names of the host routing counters.
pub const LABELS: [&str; 7] = [
    "down",
    "test_error",
    "no_interest",
    "dispatch",
    "dispatch_error",
    "passed",
    "eaten",
];
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
/// The observed result of the test and optional dispatch callbacks.
pub enum DownRoute {
    /// Test rejected evidence or outcome.
    TestRejected {
        /// Exact native test HRESULT.
        result: i32,
        /// Interest reported by the native test callback.
        interest: bool,
    },
    /// Dispatched evidence or outcome.
    Dispatched {
        /// Exact native dispatch HRESULT.
        result: i32,
        /// Consumption reported by the native dispatch callback.
        eaten: bool,
    },
}
/// Dispatch a key only after a successful interested test callback.
pub fn route_down(
    test: impl FnOnce(&mut bool) -> i32,
    dispatch: impl FnOnce(&mut bool) -> i32,
) -> DownRoute {
    let mut interest = false;
    let result = test(&mut interest);
    if result < 0 || !interest {
        return DownRoute::TestRejected { result, interest };
    }
    let mut eaten = false;
    let result = dispatch(&mut eaten);
    DownRoute::Dispatched { result, eaten }
}
impl DownRoute {
    /// Require a successful dispatch callback that explicitly consumed the key.
    pub fn consumed(self) -> bool {
        matches!(
            self,
            Self::Dispatched {
                result: 0..,
                eaten: true
            }
        )
    }
}
#[derive(Clone, Debug, Default, Eq, PartialEq)]
/// Host routing counters and the first failed native call.
pub struct Snapshot {
    /// Monotonic host publication number.
    pub publication: u32,
    /// Seven counters in LABELS order.
    pub counts: [u32; 7],
    /// First failed call, where true identifies KeyDown and false TestKeyDown.
    pub first_error: Option<(bool, i32)>,
}
impl Snapshot {
    /// Accumulate the observed routing outcome with saturating counters.
    pub fn record(&mut self, route: DownRoute) {
        self.counts[0] = self.counts[0].saturating_add(1);
        let (index, failure) = match route {
            DownRoute::TestRejected {
                result: i32::MIN..=-1,
                ..
            } => (
                1,
                match route {
                    DownRoute::TestRejected { result, .. } => Some((false, result)),
                    _ => unreachable!(),
                },
            ),
            DownRoute::TestRejected { .. } => (2, None),
            DownRoute::Dispatched { result, eaten } => {
                self.counts[3] = self.counts[3].saturating_add(1);
                if result < 0 {
                    (4, Some((true, result)))
                } else {
                    (if eaten { 6 } else { 5 }, None)
                }
            }
        };
        self.counts[index] = self.counts[index].saturating_add(1);
        if self.first_error.is_none() {
            self.first_error = failure;
        }
    }
    /// Report exhausted publication or lifecycle counters.
    pub fn saturated(&self) -> bool {
        self.publication == u32::MAX || self.counts.contains(&u32::MAX)
    }
    /// Format the established canonical status row.
    pub fn format(&self) -> String {
        let mut result = format!("Keyrill host routing v1; publication={}", self.publication);
        for (label, count) in LABELS.into_iter().zip(self.counts) {
            result.push_str(&format!("; {label}={count}"));
        }
        result.push_str(&format!(
            "; saturated={}; first_error=",
            u32::from(self.saturated())
        ));
        match self.first_error {
            None => result.push_str("none"),
            Some((dispatch, error)) => result.push_str(&format!(
                "{}/0x{:08X}",
                if dispatch { "KeyDown" } else { "TestKeyDown" },
                error as u32
            )),
        }
        result
    }
    /// Parse an exact canonical command, case name, or status representation.
    pub fn parse(text: &str) -> Option<Self> {
        if text.len() >= 512 {
            return None;
        }
        let fields: Vec<_> = text.split("; ").collect();
        if fields.len() != 11 || fields[0] != "Keyrill host routing v1" {
            return None;
        }
        let mut result = Self {
            publication: fields[1].strip_prefix("publication=")?.parse().ok()?,
            ..Default::default()
        };
        for (field, value) in fields[2..9].iter().zip(&mut result.counts) {
            *value = field.split_once('=')?.1.parse().ok()?;
        }
        let error = fields[10].strip_prefix("first_error=")?;
        if error != "none" {
            let (call, hex) = error.split_once("/0x")?;
            let dispatch = match call {
                "TestKeyDown" => false,
                "KeyDown" => true,
                _ => return None,
            };
            let error = u32::from_str_radix(hex, 16).ok()? as i32;
            if error >= 0 {
                return None;
            }
            result.first_error = Some((dispatch, error));
        }
        (result.format() == text).then_some(result)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn routing_tests_dispatch_only_after_successful_interest() {
        for tested in [-1, 0, 1] {
            for interest in [false, true] {
                for handled in [-1, 0, 1] {
                    for eaten in [false, true] {
                        let mut dispatched = false;
                        let route = route_down(
                            |value| {
                                *value = interest;
                                tested
                            },
                            |value| {
                                dispatched = true;
                                *value = eaten;
                                handled
                            },
                        );
                        assert_eq!(dispatched, tested >= 0 && interest);
                        assert_eq!(
                            route.consumed(),
                            tested >= 0 && interest && handled >= 0 && eaten
                        );
                        let mut snapshot = Snapshot::default();
                        snapshot.record(route);
                        assert_eq!(Snapshot::parse(&snapshot.format()), Some(snapshot));
                    }
                }
            }
        }
    }
    #[test]
    fn canonical_routing_rejects_extra_and_noncanonical_numbers() {
        let text = Snapshot::default().format();
        assert!(Snapshot::parse(&(text.clone() + " ")).is_none());
        assert!(Snapshot::parse(&text.replace("publication=0", "publication=00")).is_none());
        assert!(Snapshot::parse(&text.replace("saturated=0", "saturated=1")).is_none());
    }
}
