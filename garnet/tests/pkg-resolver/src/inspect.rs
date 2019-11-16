// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fuchsia_async as fasync,
    fuchsia_inspect::{assert_inspect_tree, reader::PartialNodeHierarchy},
    std::convert::TryFrom,
};

#[fasync::run_singlethreaded(test)]
async fn test_initial_inspect_state() {
    let env = TestEnv::new();
    // Wait for inspect VMO to be created
    env.proxies
        .rewrite_engine
        .test_apply("fuchsia-pkg://test")
        .await
        .expect("fidl call succeeds")
        .expect("test apply result is ok");

    // When `glob` is matching a path component that is a string literal, it uses
    // `std::fs::metadata()` to test the existence of the path instead of listing the parent dir.
    // `metadata()` calls `stat`, which creates and destroys an fd in fdio.
    // When the fd is for "root.inspect", which is a VMO, destroying the fd calls
    // `zxio_vmofile_release`, which makes a fuchsia.io.File.Seek FIDL call.
    // This FIDL call is received by `ServiceFs`, which, b/c "root.inspect" was opened
    // by fdio with `OPEN_FLAG_NODE_REFERENCE`, is treating the zircon channel as a stream of
    // Node requests.
    // `ServiceFs` then closes the channel and logs a
    // "ServiceFs failed to parse an incoming node request: UnknownOrdinal" error (with
    // the File.Seek ordinal).
    // `ServiceFs` closing the channel is seen by `metadata` as a `BrokenPipe` error, which
    // `glob` interprets as there being nothing at "root.inspect", so the VMO is not found.
    // To work around this, we use a trivial pattern in the "root.inspect" path component,
    // which prevents the `metadata` shortcut.
    //
    // To fix this, `zxio_vmofile_release` probably shouldn't be unconditionally calling
    // `fuchsia.io.File.Seek`, because, per a comment in `io.fidl`, that is not a valid
    // method to be called on a `Node` opened with `OPEN_FLAG_NODE_REFERENCE`.
    // `zxio_vmofile_release` could determine if the `Node` were opened with
    // `OPEN_FLAG_NODE_REFERENCE` (by calling `Node.NodeGetFlags` or `File.GetFlags`).
    // Note that if `zxio_vmofile_release` starts calling `File.GetFlags`, `ServiceFs`
    // will need to stop unconditionally treating `Node`s opened with `OPEN_FLAG_NODE_REFERNCE`
    // as `Node`s.
    // TODO(fxb/40888)
    let pattern = format!(
        "/hub/r/{}/*/c/pkg_resolver.cmx/*/out/objects/root.i[n]spect",
        glob::Pattern::escape(&env.nested_environment_label)
    );
    let paths = glob::glob_with(
        &pattern,
        glob::MatchOptions {
            case_sensitive: true,
            require_literal_separator: true,
            require_literal_leading_dot: false,
        },
    )
    .expect("glob pattern successfully compiles");
    let mut paths = paths.collect::<Result<Vec<_>, _>>().unwrap();
    assert_eq!(paths.len(), 1, "{:?}", paths);
    let path = paths.pop().unwrap();

    // Obtain VMO and convert into node heirarchy
    let vmo_file = File::open(path).expect("file exists");
    let vmo = fdio::get_vmo_copy_from_file(&vmo_file).expect("vmo exists");
    let hierarchy = PartialNodeHierarchy::try_from(&vmo).expect("create hierarchy from vmo");

    assert_inspect_tree!(
       hierarchy,
        root: {
            rewrite_manager: {
                dynamic_rules: {},
                dynamic_rules_path: format!("{:?}", Some(std::path::Path::new("/data/rewrites.json"))),
                static_rules: {},
                generation: 0u64,
            },
            main: {
              channel: {
                tuf_config_name: format!("{:?}", Option::<String>::None),
                channel_name: format!("{:?}", Option::<String>::None),
              }
            },
            experiments: {
            }
        }
    );

    env.stop().await;
}
