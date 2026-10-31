# Pico ST7735S Partial Refresh Design

**Date:** 2026-10-31
**Status:** Design approved; awaiting specification review
**Scope:** Eliminate the visible flash caused by periodic full-screen redraws while preserving the current 128×160 layouts and one-second update cadence.

## Problem

The application redraws its clock or dashboard every second and when a fresh metrics packet arrives. Both `draw_clock` and `draw_dashboard` start with `canvas.fill(BG)`, then resend every label and value over SPI. This clears the panel before restoring the content, producing a visible flash.

The ST7735 driver already supports bounded writes through `fill_rect`, text drawing, and address-window writes. The application can update changing regions without clearing the panel.

## Goals

- Preserve the current clock and dashboard layouts, fonts, colors, and one-second data cadence.
- Use a full-screen draw for initial display and transitions between clock and dashboard modes.
- Update only the time region when the displayed second changes.
- Update only dashboard values and progress bars whose rendered values change.
- Update date, weekday, lunar text, Wi-Fi state, time-sync state, and configuration warnings only when their displayed values change.
- Keep unchanged regions stable between updates.

## Design

The application will retain the last rendered display state and pass it to the display layer. The display layer will compare display-ready values, then repaint the smallest existing layout region that fully contains each changed value. Each partial repaint will restore that region's background before drawing text or bars, preventing remnants when text width or bar length shrinks.

The first draw after boot and each clock/dashboard mode transition will clear and redraw the full layout. Within a mode, the clock face will update its bounded time band; the dashboard will update individual metric cards and receive/transmit text rows. The colon remains steady. Status and date regions update only when their visible content changes.

The render cache belongs to the active application/display instance and starts empty on boot. This ensures the first frame is complete and avoids stale module-level state across reinitialization. Metric freshness and the three-second PC-offline transition remain governed by the existing application state.

## Alternatives considered

1. **Partial region redraw (selected):** preserves the one-second cadence and changes only the content that moved. It uses the driver's existing bounded-write path.
2. **Reduce the full-screen redraw rate:** decreases how often the flash occurs, while leaving the flash and making displayed metrics less current.
3. **Transmit a complete framebuffer each second:** still updates the panel progressively over SPI and adds a full-frame memory buffer without removing the visible redraw.

## Failure and transition behavior

- A clock/dashboard mode change triggers one complete redraw so content from the prior layout is removed.
- Wi-Fi, synchronization, configuration-warning, date, and lunar changes repaint only their own regions.
- Invalid or missing metric fields continue to use the current placeholder rendering; changes to or from a placeholder repaint that field's region.
- If a partial region cannot fit the panel bounds, the existing bounded driver behavior remains in effect; layout coordinates and region sizes will be checked against 128×160.

## Acceptance

- A second render with unchanged display-ready values issues no display writes.
- A changed clock second repaints the time region without calling the full-screen fill path.
- A changed metric repaints only its card or rate row; unchanged cards remain untouched.
- A date, lunar, Wi-Fi, or sync change repaints its associated region.
- Initial render and each mode transition perform one full-screen redraw.
- Existing unit tests continue to pass, and new recording-canvas checks verify the redraw boundaries.
- After deployment, the user confirms that the one-second clock/metrics updates no longer create a full-screen flash.

## Out of scope

This change does not alter layout, fonts, colors, update cadence, network protocol, Wi-Fi behavior, lunar conversion, or the display hardware.
