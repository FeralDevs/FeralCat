# Scrollable Settings Rail

> 🐱 **Custom** — built for this fork (not in the original firmware).

The detailed settings (the **gear** button) use a **scrollable side rail** of
tabs instead of LVGL's built-in tab bar, which can't scroll and just shrinks the
tabs as you add more.

Tabs: **Display, Sound, Connect, Time, System, Backup** — scroll the left rail to
reach them; the active tab is highlighted and scrolls into view. There's room to
keep adding sections without cramping.

*(Implementation note: the LVGL tabview still manages the content pages; its
button-matrix bar is hidden and driven by a custom scrollable button column.)*

A hidden **Debug** tab (live input monitor) exists behind a compile flag
`MEOWKIT_DEBUG_TAB` — off in releases. See [[Hardware and Gotchas]].
