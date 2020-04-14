// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{
        data::Data,
        puppet, results,
        trials::{self, Step},
        validate,
    },
    anyhow::{bail, Error},
    fidl_test_inspect_validate::TestResult,
    fuchsia_inspect::testing::{ComponentSelector, InspectDataFetcher},
};

pub async fn run_all_trials(url: &str, results: &mut results::Results) {
    let mut trial_set = trials::real_trials();
    for trial in trial_set.iter_mut() {
        match puppet::Puppet::connect(url).await {
            Ok(mut puppet) => {
                if results.test_archive {
                    match puppet.publish().await {
                        Ok(TestResult::Ok) => {}
                        Ok(result) => {
                            results.error(format!("Publish reported {:?}", result));
                            return;
                        }
                        Err(e) => {
                            results.error(format!("Publish error: {:?}", e));
                            return;
                        }
                    }
                }
                let mut data = Data::new();
                if let Err(e) = run_trial(&mut puppet, &mut data, trial, results).await {
                    results.error(format!("Running trial {}, got failure:\n{}", trial.name, e));
                } else {
                    results.log(format!("Trial {} succeeds", trial.name));
                }
                if results.test_archive {
                    match puppet.unpublish().await {
                        Ok(TestResult::Ok) => {}
                        Ok(result) => {
                            results.error(format!("Unpublish reported {:?}", result));
                            return;
                        }
                        Err(e) => {
                            results.error(format!("Unpublish error: {:?}", e));
                            return;
                        }
                    }
                }
            }
            Err(e) => {
                results.error(format!(
                    "Failed to form Puppet - error {:?} - URL may be invalid: {}.",
                    e, url
                ));
            }
        }
    }
}

async fn run_trial(
    puppet: &mut puppet::Puppet,
    data: &mut Data,
    trial: &mut trials::Trial,
    results: &mut results::Results,
) -> Result<(), Error> {
    let trial_name = format!("{}:{}", puppet.name(), trial.name);
    // We have to give explicit type here because compiler can't deduce it from None option value.
    try_compare::<validate::Action>(data, puppet, &trial_name, -1, None, -1, &results).await?;
    for (step_index, step) in trial.steps.iter_mut().enumerate() {
        match step {
            Step::Actions(actions) => {
                run_actions(actions, data, puppet, &trial.name, step_index, results).await?;
            }
            Step::WithMetrics(actions, step_name) => {
                run_actions(actions, data, puppet, &trial.name, step_index, results).await?;
                results.remember_metrics(puppet.metrics()?, &trial.name, step_index, step_name);
            }
            Step::LazyActions(actions) => {
                run_lazy_actions(actions, data, puppet, &trial.name, step_index, results).await?;
            }
        };
    }
    Ok(())
}

async fn run_actions(
    actions: &mut Vec<validate::Action>,
    data: &mut Data,
    puppet: &mut puppet::Puppet,
    trial_name: &str,
    step_index: usize,
    results: &mut results::Results,
) -> Result<(), Error> {
    for (action_number, action) in actions.iter_mut().enumerate() {
        if let Err(e) = data.apply(action) {
            bail!(
                "Local-apply error in trial {}, step {}, action {}: {:?} ",
                trial_name,
                step_index,
                action_number,
                e
            );
        }
        match puppet.apply(action).await {
            Err(e) => {
                bail!(
                    "Puppet-apply error in trial {}, step {}, action {}: {:?} ",
                    trial_name,
                    step_index,
                    action_number,
                    e
                );
            }
            Ok(validate::TestResult::Ok) => {}
            Ok(validate::TestResult::Unimplemented) => {
                results.unimplemented(puppet.name(), action);
                return Ok(());
            }
            Ok(bad_result) => {
                bail!(
                    "In trial {}, puppet {} reported action {:?} was {:?}",
                    trial_name,
                    puppet.name(),
                    action,
                    bad_result
                );
            }
        }
        try_compare(
            data,
            puppet,
            &trial_name,
            step_index as i32,
            Some(action),
            action_number as i32,
            &results,
        )
        .await?;
    }
    Ok(())
}

async fn run_lazy_actions(
    actions: &mut Vec<validate::LazyAction>,
    data: &mut Data,
    puppet: &mut puppet::Puppet,
    trial_name: &str,
    step_index: usize,
    results: &mut results::Results,
) -> Result<(), Error> {
    for (action_number, action) in actions.iter_mut().enumerate() {
        if let Err(e) = data.apply_lazy(action) {
            bail!(
                "Local-apply_lazy error in trial {}, step {}, action {}: {:?} ",
                trial_name,
                step_index,
                action_number,
                e
            );
        }
        match puppet.apply_lazy(action).await {
            Err(e) => {
                bail!(
                    "Puppet-apply_lazy error in trial {}, step {}, action {}: {:?} ",
                    trial_name,
                    step_index,
                    action_number,
                    e
                );
            }
            Ok(validate::TestResult::Ok) => {}
            Ok(validate::TestResult::Unimplemented) => {
                results.unimplemented(puppet.name(), action);
                return Ok(());
            }
            Ok(bad_result) => {
                bail!(
                    "In trial {}, puppet {} reported action {:?} was {:?}",
                    trial_name,
                    puppet.name(),
                    action,
                    bad_result
                );
            }
        }
        try_compare(
            data,
            puppet,
            &trial_name,
            step_index as i32,
            Some(action),
            action_number as i32,
            &results,
        )
        .await?;
    }
    Ok(())
}

async fn try_compare<ActionType: std::fmt::Debug>(
    data: &Data,
    puppet: &puppet::Puppet,
    trial_name: &str,
    step_index: i32,
    action: Option<&ActionType>,
    action_number: i32,
    results: &results::Results,
) -> Result<(), Error> {
    match puppet.read_data().await {
        Err(e) => {
            bail!(
                "Puppet-read error in trial {}, step {}, action {} {:?}: {:?} ",
                trial_name,
                step_index,
                action_number,
                action,
                e
            );
        }
        Ok(puppet_data) => {
            if let Err(e) = data.compare(&puppet_data, results.diff_type) {
                bail!(
                    "Compare error in trial {}, step {}, action {}:\n{:?}:\n{} ",
                    trial_name,
                    step_index,
                    action_number,
                    action,
                    e
                );
            }
        }
    }
    if results.test_archive {
        if !data.is_empty() {
            assert_ne!(
                0,
                InspectDataFetcher::new()
                    .add_selector(ComponentSelector::new(vec![
                        puppet.environment_name().to_string(),
                        puppet.component_name(),
                    ]))
                    .get()
                    .await?
                    .len()
            );
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::trials::tests::trial_with_action, crate::*, fidl_test_inspect_validate::*,
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn unimplemented_works() -> Result<(), Error> {
        let mut int_maker = trial_with_action(
            "foo",
            create_numeric_property!(
            parent: ROOT_ID, id: 1, name: "int", value: Number::IntT(0)),
        );
        let mut uint_maker = trial_with_action(
            "foo",
            create_numeric_property!(
            parent: ROOT_ID, id: 2, name: "uint", value: Number::UintT(0)),
        );
        let mut results = results::Results::new();
        let mut puppet = puppet::tests::local_incomplete_puppet().await?;
        let mut data = Data::new();
        // results contains a list of the _un_implemented actions. local_incomplete_puppet()
        // implements Int creation, but not Uint. So results should not include Int but should
        // include Uint.
        run_trial(&mut puppet, &mut data, &mut int_maker, &mut results).await?;
        run_trial(&mut puppet, &mut data, &mut uint_maker, &mut results).await?;
        assert!(!results.to_json().contains(&format!("{}: CreateProperty(Int)", puppet.name())));
        assert!(results.to_json().contains(&format!("{}: CreateProperty(Uint)", puppet.name())));
        Ok(())
    }
}
