use std::{
    cell::{Cell, RefCell},
    rc::Rc,
};

pub struct Action {
    pub boundary: &'static str,
    pub skip: usize,
    pub run: Rc<dyn Fn()>,
}
#[derive(Default)]
pub struct HostCapsule {
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
