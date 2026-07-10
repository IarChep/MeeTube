# Design: Search page (search bar + live suggestions + typed results)

Date: 2026-07-10
Status: **approved (design), pending implementation plan**

## Context & problem

`resources/qml/pages/SearchPage.qml` is a branded non-crashing **stub** (`EmptyState`
"Coming soon"). The MainPage toolbar search icon already pushes it. The **search backend
is complete and wired** — the API-tree nodes we kept during the 2026-07-10 over-engineering
cut expose:

- `innertube.video().searchVideos(query, order)` → `VideoModel::search()` → `core::fetchVideoList`
  (Search spec) → `bodies::search`.
- `innertube.channel().searchChannels(query)` → `ChannelModel::search()`.
- `innertube.playlist().searchPlaylists(query)` → `PlaylistModel::search()`.

What is missing is (a) the UI, and (b) **query suggestions** — no autocomplete endpoint,
chain, or parser exists anywhere in the tree (nor in the cuteTube2 reference).

## Goals

- A search **input field** with live, debounced **query suggestions** as the user types.
- Suggestions come from **two sources**: local **search history** when the field is empty,
  and **live YouTube suggestions** once the user types.
- On submit (Enter) or suggestion tap: load a results list. Results are **typed**: tabs for
  **Videos / Channels / Playlists**, plus a **sort-order** selector (Videos only). Video rows
  render in the **`RelatedDelegate`** style.

## Non-goals (deferred)

- Per-row history deletion (swipe-to-remove), voice input, a mixed "all types" result feed.
- Sort orders for Channels/Playlists (`searchTypes()` offers them only Relevance).
- Any change to the completed search chains/models/bodies.

## Product decisions (from brainstorming)

- **Suggestions source:** both — history (empty field) + live (typing).
- **Result scope:** Videos + type tabs (Channels/Playlists) + sort selector.

## Architecture

One page (`SearchPage.qml`) with two visual states over a shared branded red header:

```
┌ MeeTube: Search ────────────────┐   (standard app header)
│ [ 🔍 query…                 ✕ ] │   TextField (search style + clear)
├─────────────────────────────────┤
│  suggestions overlay  │ results │   mutually exclusive:
│  (ListView of strings)│ (tabs + │   overlay shown while the field has focus and
│  history ⟳ / live 🔍  │ ListView)│  the query is not yet submitted; results shown after submit
└─────────────────────────────────┘
```

- **Input:** `TextField` (com.nokia.meego 1.1), search platform style, clear button,
  `Qt.ImhNoPredictiveText`, placeholder "Search YouTube".
- **Debounce:** a QML `Timer` (250 ms, `repeat:false`) restarted on `onTextChanged`; on trigger
  it calls `suggest.query(field.text)`. Empty text also fires (→ history).
- **Submit:** `Keys`/`onAccepted` or a suggestion tap → `suggest.record(q)`, hide the overlay,
  set the pending query, load the active tab.
- **Results:** a `ButtonRow` of three checkable buttons (Videos/Channels/Playlists). One
  `ListView` whose `model` + `delegate` switch on the active tab. Each tab lazily invokes its
  search on first activation (models are cached inside the API-tree node, so re-selecting a tab
  does not re-list). Loading / empty / error overlays bind to the active model's
  `status` / `errorString` / `count` (the shared `ServiceListModel` API).
- **Sort:** a toolbar `ToolIcon` (enabled only on the Videos tab) opens a `SelectionDialog`
  populated from `innertube.searchTypes()` video orders (Relevance/Date/Views/Rating); choosing
  one re-runs `searchVideos(q, order)`.

### Backend — suggestions only

Three additions, each following an existing pattern.

1. **Chain** — `core::fetchSearchSuggestions(IHttp &http, const QString &query,
   const JobToken &job, std::function<void(const Outcome<QStringList> &)> done)`
   (`src/core/core/chains.{h,cpp}`). Issues `http.get(url, job, …)` to
   `https://suggestqueries.google.com/complete/search?client=firefox&ds=yt&hl=<hl>&q=<pct-encoded>`
   with `hl` read from `http.session()`. Runs on the worker thread like every other chain;
   anonymous (no bearer), so the `net::CurlNetworkReply` redirect/SSRF guards are satisfied.
   The suggest base URL is added to `innertube/catalog.h` (a rotating constant).

2. **Parser** — `QStringList parseSuggestions(std::string_view)`
   (`src/core/parsers/suggestparser.{h,cpp}`). The `client=firefox` response is a 2-element
   JSON array `["query", ["s1","s2", …]]`; a Glaze read into
   `std::tuple<std::string, std::vector<std::string>>` yields the suggestion vector. Malformed
   or empty input → empty list (never throws).

3. **QObject** — `SearchSuggest` (`src/core/innertube/searchsuggest.{h,cpp}`), a GUI-affine
   QObject reaching the backend via `Innertube::instance()->apiRef()` exactly like `StreamSet`:
   - `Q_INVOKABLE void query(const QString &q)` — cancels the previous job (`JobToken`),
     posts `fetchSearchSuggestions` through `apiRef().host->invoke(...)`, delivers back via
     `invokeGui` (guarded by `live(job)`). When `q` is empty it skips the network and returns
     the stored history instead.
   - `QStringList results` (`Q_PROPERTY` + `resultsChanged()`), `bool live` (history vs. live —
     lets the delegate pick a history/search glyph).
   - `Q_INVOKABLE void record(const QString &q)` — prepend to a capped (~15) recent list,
     de-duplicated, persisted via `QSettings` under `search/history` (same store default as
     `AccountStore`).
   - Registered `qmlRegisterType<yt::SearchSuggest>("MeeTube", 1, 0, "SearchSuggest")` in
     `src/app/main.cpp`; QML instantiates `SearchSuggest { id: suggest }` and binds
     `model: suggest.results`.

   Debouncing lives in QML (the `Timer`), keeping the QObject stateless per call and the
   transport dumb — one GET per settled keystroke, previous in-flight GET canceled.

### QML files

- **Rewrite** `resources/qml/pages/SearchPage.qml` — field, debounce, suggestions overlay,
  tabbed results, sort dialog. Suggestion rows are an inline delegate (glyph + text).
- **New** `resources/qml/components/delegates/ChannelDelegate.qml` — a channel result row
  (squircle `Avatar` + username + subscriber count), bound to `ChannelModel` roles
  `id/username/thumbnailUrl/subscriberCount`. Layout lifted from the ManageSubscriptionsPage
  inline row (minus the unsubscribe button); ManageSubscriptionsPage is left unchanged.
- Video rows reuse `RelatedDelegate`; playlist rows reuse `PlaylistDelegate`.

## Data flow

```
type → Timer(250ms) → suggest.query(text)
                        ├ text=="" → results = history (QSettings)          → resultsChanged
                        └ text!="" → apiRef.host.invoke → fetchSearchSuggestions
                                       → get(suggest URL) → parseSuggestions → invokeGui
                                       → results = suggestions               → resultsChanged
tap suggestion / Enter → suggest.record(q); q submitted; overlay hidden
active tab (Videos|Channels|Playlists):
   Videos    → innertube.video().searchVideos(q, order)  → RelatedDelegate
   Channels  → innertube.channel().searchChannels(q)     → ChannelDelegate
   Playlists → innertube.playlist().searchPlaylists(q)   → PlaylistDelegate
sort icon (Videos tab) → SelectionDialog(searchTypes video orders) → searchVideos(q, order')
```

## Error handling

- Suggestions: any transport/parse failure → empty `results` (the overlay simply shows nothing
  or the history). Never blocks typing; a canceled job is dropped by the `live(job)` gate.
- Results: the active model's `status`/`errorString` drive a loading spinner / empty state /
  error overlay, identical to the existing feed pages.
- Suggest endpoint is a third-party host: HTTPS only (SSRF guard), anonymous (no credential
  replay on redirect), same bounded-redirect path as any credential-free GET.

## Testing

- `tst_meetube_parsers`: `parseSuggestions` — a normal `["q",["a","b","c"]]` payload → 3 items;
  empty/garbage → empty list.
- `tst_meetube_chains`: `fetchSearchSuggestions` — `FakeHttp` queues the array JSON for the
  suggest URL → the `done` callback receives the expected `QStringList`; a canceled job delivers
  nothing.
- Search-history persistence (QSettings) is thin; verified by a manual simulator run rather than
  a dedicated unit test (YAGNI).
- Host suite must stay green (currently 9/9). Device verification of the live suggest endpoint +
  history is a follow-up (consistent with the project's pending on-device checklist).

## Files touched

New:
- `src/core/parsers/suggestparser.{h,cpp}`
- `src/core/innertube/searchsuggest.{h,cpp}`
- `resources/qml/components/delegates/ChannelDelegate.qml`

Modified:
- `src/core/core/chains.{h,cpp}` (+`fetchSearchSuggestions`, +`Outcome<QStringList>` if not present)
- `src/core/innertube/catalog.h` (+suggest base URL)
- `src/core/CMakeLists.txt` (+2 sources)
- `src/app/main.cpp` (+`qmlRegisterType<SearchSuggest>`)
- `resources/qml/pages/SearchPage.qml` (full rewrite)
- `resources/resources.qrc` (+ChannelDelegate.qml)
- `tests/tst_meetube_parsers.cpp`, `tests/tst_meetube_chains.cpp` (+cases)
