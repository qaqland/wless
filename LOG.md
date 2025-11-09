# LOG in WIP

## 2025-10-29

### Config

getopt + getsubopt

```bash
-d  enable wlr-debug
-h  print usage
-v  version
-o  mod1,mod2,key=dot,cmd=hello (keybinding)
-s  start-cmd
-r  path of lanuncher
-t  path of terminal
```

`~/.wless.rc` store long arguments

### Jump-Or-Exec

place this implement outside wless, third patry clients could use

- `ext_foreign_toplevel_list_v1` to get list of windows
- `xdg_activation` to active the matched one or decide to start a new one

e.g.

```bash
jump-or-exec -j REGEX -- COMMAND
```

more details are passed through environments

## 2025-11-01

- alt + tab
- win + tab

### Switcher Fallback

remove the first one and insert it into the last

- HEAD, A, B, C, D
- A, HEAD, B, C, D

### Switcher Outsider

set it as a lr-layer-shell tool

if we cannot get output message from protocols, wm would just reduce events

- wlr-foreign-toplevel-management-unstable-v1
- ext-foreign-toplevel-list-v1

## 2025-11-02

> output: use backend commits
> Pass the whole new desired state to the backend, so that the
> backend can leverage KMS atomic commits.
>
> <https://github.com/cage-kiosk/cage/commit/3da3ec0c2776594a546478f0fc31f96ef74e03ac>

there are some new APIs:

- `wlr_output_swapchain_manager_init`
- `wlr_output_swapchain_manager_prepare`
- `wlr_output_swapchain_manager_get_swapchain`
- `wlr_scene_output_build_state`
- `wlr_backend_commit`
- `wlr_output_swapchain_manager_apply`
- `wlr_output_swapchain_manager_finish`

## 2025-11-03

TODO: need one SDL3 test demo

- resize slowly

## 2025-11-04

bug one usb-disk to install linux-to-go to test
