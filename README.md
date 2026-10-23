# Peppy

Peppy is a native Adobe InDesign plug-in that lets AI agents work in your open
InDesign documents over MCP. It's the InDesign sibling of
[Slippy](https://tonykastaneda.github.io/slippy/), which does the same for
Illustrator and Photoshop. Peppy follows Slippy's design, and its panel is
adapted from Slippy's code, shared with permission (see [CREDITS](CREDITS.md)).
Where Slippy is a frog, Peppy is a hare: gray fur, tall ears, buck teeth.

```
agent ──MCP──▶ 127.0.0.1:7333/mcp/<token> ─▶ Peppy.InDesignPlugin ─▶ InDesign C++ SDK
shell ─▶ client/peppy ─▶ /rpc/<token> ──────┘   (server threads)   (main thread, one undo step per edit)
```

## Principles (taken from Slippy)

- **One call, one undo step**, named after what it did ("Peppy: item.transform").
  `peppy_batch` makes several calls into a single step.
- **Writes run alone.** A save or export never sits inside an edit step. A
  batch splits around them. Opening, closing or switching documents ends a run.
- **Never saves by surprise, and never opens a dialog.** `document.close` discards
  changes unless `save: true`.
- **Honest errors in one shape:** `{code, message, data.hint}`, where the hint
  says what to try next.
- **Small surface, full reach.** Agents get about 20 everyday tools, plus
  `peppy_find` / `peppy_call` for the rest of the roughly 80 commands, and
  `script.run` as a last resort.
- **Ids are InDesign's own UIDs**, which stay stable across saves. Units are
  points, and coordinates are page coordinates (top-left origin, y down).

## Status

| Part | State |
|---|---|
| `shared/`: HTTP server, token, MCP, dispatcher (runs, undo steps, `$0.id` references, quiet), narration, command catalog | done, tested |
| `mock/`: the whole stack on an in-memory document, no SDK needed | done |
| `tests/test_protocol.py`: 26 checks against the mock | passing |
| `client/peppy`: CLI over JSON-RPC | done |
| `panel/`: the panel with Peppy the hare (ears flop asleep, perk awake, pin back on errors; nose twitches), bars, feed, agent badge, terminal drawer | done; `make preview` runs it live on the mock |
| `indesign/`: the plug-in: startup service, idle-task main-thread queue, undo steps as command sequences, Window > Peppy, the panel, and the commands (`peppy.jsx`, InDesign's scripting DOM run in-process in a persistent engine; undo/redo, script.run and screenshots native) | runs in InDesign 2026: MCP, batches as one undo step, screenshots verified |

## Version

The version lives in one place, `VERSION`. The plug-in (its resources, Info.plist
and `app.info`), the mock and the panel all take it from there at build time.

## Build

```sh
make mock && build/peppy-mock    # serves the mock on 127.0.0.1:7343 (PEPPY_PORT to change)
make test                        # protocol tests against the mock
make preview                     # the panel, live on the mock (agents can connect), with made-up demo calls
make plugin                      # the InDesign plug-in, once the SDK is at ~/Developer/AdobeInDesignSDK
```

To get the SDK, sign in at https://developer.adobe.com/console, open
**Downloads**, choose **InDesign**, and download the **InDesign 2026 Plug-in SDK**
for macOS. Unzip it to `~/Developer/AdobeInDesignSDK`.

**The SDK's resource compiler and Gatekeeper.** `devtools/bin/odfrc-cmd` comes
quarantined and only ad hoc signed, and macOS holds it at launch, forever, with
no error. Clear the quarantine, then keep a re-signed copy under another name,
which the Makefile prefers:

```sh
xattr -dr com.apple.quarantine ~/Developer/AdobeInDesignSDK
cp ~/Developer/AdobeInDesignSDK/devtools/bin/odfrc-cmd /tmp/odfrc-x && codesign --force --sign - /tmp/odfrc-x
mv /tmp/odfrc-x ~/Developer/AdobeInDesignSDK/devtools/bin/odfrc-x
```

**Installing.** InDesign's `Plug-Ins` folder belongs to root, so once, make a
`Peppy` folder there that's yours; after that `make install` needs no sudo.
Quit InDesign first.

```sh
sudo mkdir -p "/Applications/Adobe InDesign 2026/Plug-Ins/Peppy" && sudo chown $USER "/Applications/Adobe InDesign 2026/Plug-Ins/Peppy"
make plugin && make -C indesign install
```

## Windows

`indesign/win` builds `Peppy.pln` with Visual Studio 2022. Tested in InDesign
2026 (21.6) on Windows 10: MCP, batches as one undo step, screenshots, export
and save paths (including a OneDrive-redirected Desktop).

- **Build Tools:** Visual Studio 2022 Build Tools with the C++ workload, the
  Windows 11 SDK 10.0.26100, and the **MSVC 14.42** toolset
  (`Microsoft.VisualStudio.Component.VC.14.42.17.12.x86.x64`). The InDesign 2026
  SDK refuses newer compilers (`source\precomp\msvc\xlocnum_hack.h`).
- **SDK:** the same plug-in SDK zip, unzipped to `C:\sdk\AdobeInDesignSDK` (use
  `tar -xf`; `Expand-Archive` takes hours on it).
- **Build / install** (quit InDesign first to install):
  ```powershell
  powershell -ExecutionPolicy Bypass -File indesign\win\build.ps1 -Install
  ```
  It copies `Peppy.vcxproj` into the SDK's `build\win\prj` (the SDK's property
  sheets use paths relative to it), builds, and puts `Peppy.pln` and
  `(Peppy Resources)` in `Plug-Ins\Peppy`.
- The token and session live in `%APPDATA%\Peppy`.
- **Panel:** Peppy the hare in a floating tool window above InDesign (GDI+,
  adapted from Slippy's Windows panel): moods, bars, feed, agent badge and Copy
  connection, like the Mac. Window > Peppy shows and hides it; right-click it
  for Pause agents. No terminal drawer on Windows.

## How the plug-in hooks in (plan)

- **Startup:** an `IStartupShutdownService` on `kSessionBoss` registers the
  catalog and starts `peppy::Server`. Shutdown stops it.
- **Main thread:** `Host::Post` pushes onto a queue. An `IIdleTask` drains the
  queue on InDesign's main thread, waking itself immediately when work arrives.
- **Undo:** `BeginStep` maps to `CmdUtils::BeginCommandSequence` plus `SetName`.
  `EndStep(false)` sets the global error code before `EndCommandSequence`, so
  InDesign rolls the step back.
- **Ready:** calls are refused with a clear error while a modal dialog is open.
- **Commands:** run natively through the SDK's facades and command bosses
  (geometry, transform, graphic attributes, text model, styles, swatches,
  place, export providers). `script.run` uses InDesign's script manager.
- **Panel:** a floating Cocoa window with the mascot, the feed and Copy
  connection first. A docked panel (ODFRC resources) comes later.

## Connecting an agent

Once the plug-in is running, click **Copy connection** in Peppy's panel and add
`http://127.0.0.1:7333/mcp/<token>` as an MCP server (streamable HTTP). The token
lives in `~/Library/Application Support/Peppy/token`. Add `?tools=all` to the URL
to expose every command as a separate tool.

## License

MIT - see [LICENSE](LICENSE). Third-party parts keep their own terms; see
[CREDITS](CREDITS.md).
