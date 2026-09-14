# Script Runner (Berry)

Run small **[Berry](https://berry-lang.github.io/) scripts** from the SD card to
poke the LED, GPIO and buttons — no reflashing. Berry is a tiny Python-ish
language embedded in the firmware.

## Using it

1. Put `.be` files in a **`/scripts`** folder on the SD card (created for you on
   first run if missing). The examples in [`scripts/`](scripts) are a good start.
2. Open the **Script Runner** app.
3. **Up/Down** to pick a script, **A** to run, **A** again to go back to the list,
   **hold B** to exit the app.
4. `print(...)` output appears on screen. A script that runs longer than ~20 s,
   or while you **hold B**, is stopped automatically — an infinite loop can't
   hang the device.

## API

Global functions available to scripts:

| Call | Does |
|---|---|
| `print(x)` | print to the on-screen console (and serial) |
| `delay(ms)` | wait milliseconds |
| `millis()` | milliseconds since boot (int) |
| `led(r, g, b)` | set the **onboard WS2812** pixel (0–255 each) |
| `button(name)` | is a button held now? `"A" "B" "up" "down" "left" "right"` → bool |
| `pin_mode(gpio, out)` | `out` truthy = OUTPUT, else INPUT_PULLUP |
| `pin_write(gpio, v)` | drive a GPIO high/low |
| `pin_read(gpio)` | read a GPIO (0/1) |

Plus all of Berry's core: `var`, `for i : range(a,b)`, `while`, `if/elif/else`,
lists `[1,2,3]`, `..` for string concat, `str(x)`, math, etc.

> ⚠️ `pin_*` touch **raw GPIOs** — most pins on this board are wired to the
> display/SD/PSRAM/etc. Only drive pins you know are free (there's very little
> broken out). `led()` is the safe way to play with color.

## Example — blink the LED

```berry
# blink.be
print("Blinking the LED 5 times")
for i : range(1, 5)
  led(60, 0, 0)   # dim red
  delay(300)
  led(0, 0, 0)    # off
  delay(300)
  print("tick " .. str(i))
end
led(0, 0, 0)
```

See [`scripts/`](scripts) for `hello.be`, `blink.be`, `rainbow.be`, `buttons.be`.
