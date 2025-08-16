# wless(WIP)

window-less, a monocle-style wayland compositor based on wlroots.

## Install(WIP)

The officially supported Linux distributions are Alpine Linux and Deepin.

## Compile

```bash
depends="
        foot
        "
makedepends="
        libxkbcommon-dev
        meson
        wayland-dev
        wayland-protocols
        wlr-protocols
        wlroots-dev
        "
```

## Key bindings

- <kbd>Super</kbd> + <kbd>Enter</kbd> start foot terminal
- <kbd>Super</kbd> + <kbd>W</kbd> close window
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>Esc</kbd> quit wless
- <kbd>Alt</kbd> + <kbd>Tab</kbd> switch windows
- <kbd>Alt</kbd> + <kbd>Shfit</kbd> + <kbd>Tab</kbd> switch windows (reverse)
- <kbd>Super</kbd> + <kbd>Tab</kbd> switch windows (same output)
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>Tab</kbd> switch windows (same output, reverse)
- <kbd>Super</kbd> + <kbd>.</kbd> switch outputs
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>.</kbd> move client between outputs

### TODO

- <kbd>Super</kbd> + <kbd>R</kbd> starter menu
- <kbd>Super</kbd> + <kbd>Space</kbd> switcher menu
- <kbd>Super</kbd> + <kbd>0..9</kbd> exec-or-jump
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>0..9</kbd> force-exec

## Output Mode Settings

- wlr-randr
- <https://github.com/emersion/kanshi>

## License

* All rights reserved (WIP).
* No guarantees are made regarding its usability or functionality.
* This is a hobby project created in my spare time.
* Currently not accepting external code contributions.

## Acknowledgements

- tinywl(wlroots), cage, dwl
- @DreamMaoMao

Thanks to @vaaandark for donating me monitor to help develop wless.

```bash
$ wlr-randr
DP-1 "Xiaomi Corporation Mi Monitor (DP-1)"
  Make: Xiaomi Corporation
  Model: Mi Monitor
  Physical size: 800x330 mm
  Enabled: yes
```

