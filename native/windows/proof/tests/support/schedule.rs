use std::{
    cell::{Cell, RefCell},
    rc::Rc,
};

pub struct Action {
    pub boundary: &'static str,
    pub skip: usize,
    pub run: Rc<dyn Fn()>,
}
pub struct HostCapsule {
    pub field_facts: RefCell<Option<super::dll::FieldFacts>>,
    pub layouts: RefCell<Vec<isize>>,
    pub layout_results: RefCell<Vec<i32>>,
    pub layout_calls: Cell<usize>,
    pub panic_count: Cell<u32>,
    pub trace: RefCell<Vec<&'static str>>,
    pub actions: RefCell<Vec<Action>>,
    pub live_objects: Cell<usize>,
    pub adds: Cell<usize>,
    pub releases: Cell<usize>,
    pub final_releases: Cell<usize>,
    pub dependency_retains: Cell<usize>,
    pub dependency_releases: Cell<usize>,
}
impl HostCapsule {
    pub fn hit(&self, boundary: &'static str) {
        self.trace.borrow_mut().push(boundary);
        let action = {
            let mut actions = self.actions.borrow_mut();
            let found = actions.iter().position(|a| a.boundary == boundary);
            found.and_then(|index| {
                if actions[index].skip > 0 {
                    actions[index].skip -= 1;
                    None
                } else {
                    Some(actions.remove(index).run)
                }
            })
        };
        if let Some(action) = action {
            action();
        }
    }
    pub fn on(&self, boundary: &'static str, skip: usize, run: impl Fn() + 'static) {
        assert!(
            super::providers::BOUNDARIES.contains(&boundary),
            "unknown provider boundary: {boundary}"
        );
        self.actions.borrow_mut().push(Action {
            boundary,
            skip,
            run: Rc::new(run),
        });
    }
    pub fn check(&self) {
        assert_eq!(
            self.panic_count.get(),
            0,
            "panic contained in provider callback"
        );
    }
    pub fn drained(&self) {
        self.check();
        assert!(
            self.actions.borrow().is_empty(),
            "unconsumed provider schedules: {:?}",
            self.actions
                .borrow()
                .iter()
                .map(|a| a.boundary)
                .collect::<Vec<_>>()
        );
    }
}

impl Default for HostCapsule {
    fn default() -> Self {
        Self {
            field_facts: RefCell::new(None),
            layouts: RefCell::new(vec![0x04090409, 0x04190419]),
            layout_results: RefCell::new(vec![]),
            layout_calls: Cell::new(0),
            panic_count: Cell::new(0),
            trace: RefCell::new(vec![]),
            actions: RefCell::new(vec![]),
            live_objects: Cell::new(0),
            adds: Cell::new(0),
            releases: Cell::new(0),
            final_releases: Cell::new(0),
            dependency_retains: Cell::new(0),
            dependency_releases: Cell::new(0),
        }
    }
}
