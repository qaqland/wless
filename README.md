# wless(WIP)

window less, wlroots based wayland compositor.

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
- <kbd>Super</kbd> + <kbd>N</kbd>
- <kbd>Super</kbd> + <kbd>P</kbd>
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>N</kbd>
- <kbd>Super</kbd> + <kbd>Shfit</kbd> + <kbd>P</kbd>

## Output Mode Settings

- wlr-randr
- <https://github.com/emersion/kanshi>

## Acknowledgements

- tinywl(wlroots), cage, dwl
- @DreamMaoMao

A special thanks to @vaaandark for giving me the monitor

```bash
$ wlr-randr
DP-1 "Xiaomi Corporation Mi Monitor (DP-1)"
  Make: Xiaomi Corporation
  Model: Mi Monitor
  Physical size: 800x330 mm
  Enabled: yes
```

