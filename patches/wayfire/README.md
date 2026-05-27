# Ridux Wayfire patches

Ridux uses Wayfire as the preferred desktop/compositor path for now.

Patch layout:

```text
patches/wayfire/<repo-name>/<patch-name>.patch
```

Examples:

```text
patches/wayfire/wayfire/0001-ridux-default-session.patch
patches/wayfire/wf-shell/0001-ridux-panel-branding.patch
patches/wayfire/wayfire-plugins-extra/0001-ridux-effects.patch
```

Workflow:

```sh
make wayfire-source
make wayfire-build
make wayfire-rootfs
```

Edit the checkout under `third_party/wayfire/src/<repo>` while experimenting.
Once the change is good, turn it into a patch here so the build is repeatable.
