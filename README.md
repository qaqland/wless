# wless(WIP)

Window LESS, monocle style wayland compositor.

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

- <kbd>Super</kbd> + <kbd>Enter</kbd>
- <kbd>Super</kbd> + <kbd>W</kbd>
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>Esc</kbd>
- <kbd>Alt</kbd> + <kbd>Tab</kbd>
- <kbd>Alt</kbd> + <kbd>Shfit</kbd> + <kbd>Tab</kbd>
- <kbd>Super</kbd> + <kbd>Tab</kbd>
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>Tab</kbd>

### TODO

- <kbd>Super</kbd> + <kbd>R</kbd>
- <kbd>Super</kbd> + <kbd>Space</kbd>
- <kbd>Super</kbd> + <kbd>0..9</kbd>
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>0..9</kbd>
- <kbd>Super</kbd> + <kbd>,</kbd>
- <kbd>Super</kbd> + <kbd>.</kbd>
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>,</kbd>
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>.</kbd>

## Output Mode Settings

- wlr-randr
- <https://github.com/emersion/kanshi>

## License

* All rights reserved.
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

