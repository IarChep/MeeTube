# InnerTube API (youtubei) — полный research

> **Дата компиляции:** 2026-06-30. Исследование собрано многоагентным deep-research пайплайном
> (4 анализатора локальных исходников + 6 веб-направлений, **414 ссылок-обращений / 127 уникальных
> источников**) и сшито с battle-tested локальной базой
> [`docs/INNERTUBE_API.md`](docs/INNERTUBE_API.md) проекта MeeTube и тремя локальными проектами в
> `/opt/projects/innertube-examples/` (**Dmitry's WP YouTube**, **tombulled/innertube**,
> **LuanRT/YouTube.js**).
>
> **Назначение:** самодостаточный справочник, по которому разработчик может с нуля написать клиент
> InnerTube. Раздел 11 — отдельно про legacy/ограниченные платформы (Windows Phone, embedded, Nokia
> N9, отсутствие JS-движка).
>
> ⚠️ **О свежести.** API недокументирован и меняется часто (иногда еженедельно). Версии клиентов,
> политика PoToken и обходы помечены датой и статусом. **Хрупкие** факты (`FRAGILE`) прошли
> состязательную проверку в 3 голоса; перед продакшеном перепроверяйте их по живому исходнику
> **yt-dlp** (`yt_dlp/extractor/youtube/_base.py`). Технические значения (ключи, версии, заголовки)
> приведены как факты с источником; неподтверждённое явно помечено «**требует проверки**».

---

## 1. Обзор и история

**InnerTube** (внутреннее имя — **`youtubei`**) — это приватный, недокументированный JSON-over-HTTP
API, через который работают **все** официальные клиенты YouTube: веб, Android, iOS, Smart-TV
(living room), YouTube Music, YouTube Kids, YouTube Studio, YouTube TV/Unplugged. Это единая
backend-точка: что бы ни делал официальный клиент — открыл главную, искал, листал канал, играл
видео — он шлёт запрос на `…/youtubei/v1/<endpoint>`.

**Историческая справка (Project InnerTube).** «InnerTube» — название внутреннего проекта Google
~2014–2015 годов, который унифицировал backend YouTube под все поверхности (web/mobile/TV) и
переориентировал продукт на персонализированные рекомендации и living-room/Smart-TV опыт. Этот
рефакторинг подаётся в прессе как ключевой для конкуренции с Netflix/HBO и для роста watch-time.
Источники для вводной части (контекст продукта, не технический):
- Gizmodo, «How Project InnerTube Helped Pull YouTube Out of the Gutter».
- Fast Company, «To Take On HBO And Netflix, YouTube Had To Rewire Itself».

> _Примечание:_ публичные технические детали относятся к API, а не к внутренней истории — у Google
> нет официальной документации InnerTube, всё ниже — reverse-engineering сообщества.

**Сервисы, работающие на InnerTube** (каждый — это другой `context.client` и/или другой хост,
подтверждено tombulled/innertube `config.py` рефералами):

| Сервис | Поверхность InnerTube |
|---|---|
| YouTube (основной) | `WEB`, `MWEB`, `ANDROID`, `IOS`, `TVHTML5` |
| YouTube Music | `WEB_REMIX` (хост `music.youtube.com`), `ANDROID_MUSIC`, `IOS_MUSIC`; endpoints `music/*` |
| YouTube Kids | `WEB_KIDS`, `ANDROID_KIDS`, `IOS_KIDS`, `TVHTML5_FOR_KIDS` (реферал `youtubekids.com`) |
| YouTube Studio | `WEB_CREATOR`, `ANDROID_CREATOR`, `IOS_CREATOR` (реферал `studio.youtube.com`) |
| YouTube TV / Unplugged | `TVHTML5`, `WEB_UNPLUGGED`, `ANDROID_UNPLUGGED`, `TV_UNPLUGGED_*` |

**Главные сложности — НЕ сам API**, а две защитные подсистемы вокруг плеера:
1. **Дешифровка сигнатуры/`n`-параметра стрим-URL** (player JavaScript) — §6/§7.
2. **`po_token` / BotGuard** анти-бот аттестация — §6.

Обе можно **в значительной мере обойти выбором клиента** (`ANDROID_VR`/`IOS`/`ANDROID`) — это
центральная идея для тонких/legacy-клиентов (§5, §11).

---

## 2. InnerTube vs официальный YouTube Data API v3

Это **разные** API. Не путать. (И не путать с **Invidious** — это сторонний прокси-фронтенд, см. §10.)

| | **YouTube Data API v3** (официальный) | **InnerTube** (`youtubei`, приватный) |
|---|---|---|
| Документация | Полная, поддерживается, ToS-blessed | Нет; reverse-engineered; может сломаться без предупреждения |
| Ключ | Нужен API-ключ из Google Cloud (для публичных чтений) | Ключа можно не слать; запрос аутентифицируется блоком `context.client` + заголовками |
| Авторизация | OAuth2 для приватных/пользовательских операций | Аноним работает keyless; OAuth2 только для авторизованных чтений/действий |
| Квоты | **10 000 единиц/день**; `search.list` = **100 ед**, `videos.list` = 1 ед, `videos.insert` ≈ 1600 ед | Формальной квоты нет (очень высокие неявные лимиты; есть анти-бот/IP-репутация) |
| Формат ответа | Чистый, стабильный, санированный JSON | Сырые **renderer**-деревья (UI-описания), нужно парсить |
| Доступ к данным | Только то, что Google решил отдать | Отдаёт то, что официальный API **скрывает** (стримы, полные комментарии, главная, рекомендации, транскрипты) |
| Риск | Низкий, легальный | Может нарушать ToS; хрупкость версий/обходов |

Вывод: Data API v3 — для легального, квотируемого, метаданного доступа. InnerTube — для полного
функционала клиента (плеер, стримы, рекомендации, бесконечная лента), ценой хрупкости и серой зоны.
Источник контраста: tombulled/innertube README; общеизвестные квоты Data API v3.

---

## 3. Транспорт: хосты, endpoints (waypoints), query-параметры, INNERTUBE_API_KEY

### 3.1 Транспорт
Все запросы — **HTTPS `POST`** на `https://<host>/youtubei/v1/<endpoint>?prettyPrint=false` с телом
`{"context": {...}, <endpoint-specific fields>}`.

### 3.2 Хосты
| Хост | Назначение | Источник |
|---|---|---|
| `www.youtube.com` | основной (большинство клиентов; Dmitry WP использует именно его) | Dmitry `Config.cs`; docs/INNERTUBE_API.md |
| `youtubei.googleapis.com` | альтернативный API-хост (часто у библиотек; tombulled — единственный) | tombulled `config.py:37`; YouTube.js `Constants.ts` PRODUCTION_2 |
| `music.youtube.com` | YouTube Music (`WEB_REMIX`), endpoints `music/*` | docs/INNERTUBE_API.md §1 |
| `studio.youtube.com` | YouTube Studio (creator) | tombulled рефералы |
| `www.youtubekids.com` / `m.youtube.com` | Kids / mobile-web реферал | tombulled `config.py` |
| `green-youtubei.sandbox.googleapis.com`, `release-youtubei.sandbox.googleapis.com` | STAGING/RELEASE sandbox | YouTube.js `Constants.ts` URLS.API |

### 3.3 Endpoints (waypoints) — полный список
Метод везде **POST**. «Тело» — обязательные/ключевые поля помимо `context`.

| Endpoint | Назначение | Ключевые поля тела |
|---|---|---|
| **`player`** | Воспроизведение: `streamingData`, `videoDetails`, `playabilityStatus`, `captions`, `microformat` | `videoId`, `contentCheckOk:true`, `racyCheckOk:true`, `playbackContext.contentPlaybackContext.signatureTimestamp`, `serviceIntegrityDimensions.poToken`, `params` |
| **`next`** | Страница просмотра: связанные видео, точка входа в комментарии, панель плейлиста, метаданные | `videoId`, `playlistId`, `params`, `continuation` |
| **`browse`** | Каналы, плейлисты, главная/лента, библиотека/история/подписки, посты сообщества | `browseId` (`FEwhat_to_watch`, `FEsubscriptions`, `FElibrary`, `FEhistory`, `FEchannels`, `UC…`, `VL<playlistId>`), `params`, `continuation` |
| **`search`** | Поиск + подсказки | `query`, `params` (base64-protobuf фильтр), `continuation` |
| **`guide`** | Левое меню / список подписок | — |
| **`config`** | Bootstrap холодной/горячей конфигурации (visitorData, флаги) | — |
| **`get_transcript`** | Субтитры/транскрипт | `params` (base64-protobuf videoId+lang) |
| **`navigation/resolve_url`** | Разрешение vanity/handle URL → `browseId`/UCID | `url` |
| **`account/accounts_list`** | Список аккаунтов owner/brand (authed) | `accountReadMask` |
| **`reel/reel_item_watch`**, **`reel/reel_watch_sequence`** | Shorts: плеер + лента | reel id/index, `params` |
| **`updated_metadata`** | Обновление метаданных живого просмотра | `videoId` |
| **`like/like` · `like/dislike` · `like/removelike`** | Оценка видео (authed) | `target.videoId` |
| **`subscription/subscribe` · `/unsubscribe`** | Подписка на канал (authed) | `channelIds` |
| **`notification/get_notification_menu`** · `/get_unseen_count` | Уведомления (authed) | `continuation` |
| **`live_chat/get_live_chat`** · `live_chat/get_item_context_menu` | Live chat | `continuation` |
| **`music/get_search_suggestions`**, **`music/get_queue`** | YT Music (хост `music.youtube.com`) | `input` / `videoIds`,`playlistId` |
| **`att/get`** | Получить challenge аттестации BotGuard | `engagementType` |

> Player JS (для дешифровки, §7):
> `GET https://www.youtube.com/s/player/<player_id>/player_ias.vflset/en_US/base.js`
> (вариант `player_es6.vflset` тоже встречается; YouTube.js извлекает `player_id` из `/iframe_api`).

Источники набора endpoints: YouTube.js `Actions.ts` (`InnertubeEndpoint`), tombulled
`enums.py`/`clients.py`, Dmitry `Config.cs`, docs/INNERTUBE_API.md §3.

### 3.4 Query-параметры
- **`prettyPrint=false`** — обязателен на практике (компактный JSON; иначе ответ форматируется).
- **`key=<INNERTUBE_API_KEY>`** — **опционально**. Публичный, не ротируемый ключ, вшитый в страницу.
  yt-dlp и NewPipe ключ **не шлют вообще** — запрос валиден по `context.client` + заголовкам
  `X-YouTube-Client-Name/Version`. Но Dmitry WP и tombulled его шлют (см. ниже).
- **`alt=json`** — встречается у некоторых библиотек (tombulled всегда добавляет `alt=json`).

### 3.5 INNERTUBE_API_KEY (публичные ключи по клиентам)
Эти ключи **публичны и общеизвестны** (вшиты в HTML/ytcfg). Нужны только если вы шлёте `?key=`.
Полная таблица из tombulled `config.py` + Dmitry + YouTube.js:

| Клиент | API-ключ | Источник |
|---|---|---|
| **WEB / WEB_EMBEDDED / WEB_CREATOR** | `AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8` | tombulled `:45`; YouTube.js `Constants.ts`; Dmitry `:81` |
| ANDROID | `AIzaSyA8eiZmM1FaDVjRy-df2KTyQ_vz_yYM39w` | tombulled `:60` |
| IOS | `AIzaSyB-63vPrdThhKuerbB2N_l7Kwwcxj6yUAc` | tombulled `:67` |
| TVHTML5 | `AIzaSyDCU8hByM-4DrUqRUYnGn-3llEO78bcxq8` | tombulled `:74` |
| WEB_REMIX (Music) | `AIzaSyC9XL3ZjWddXya6X74dJoCTL-WEYFDNX30` | tombulled `:304` |
| ANDROID_MUSIC | `AIzaSyAOghZGza2MQSZkY_zfZ370N-PUdXEo8AI` | tombulled `:133` |
| IOS_MUSIC | `AIzaSyBAETezhkwP0ZWA02RsqT1zu78Fpt0bC_s` | tombulled `:146` |
| ANDROID_CREATOR | `AIzaSyD_qjV8zaaUMehtLkrKFgVeSX_Iqbtyws8` | tombulled `:99` |
| IOS_CREATOR | `AIzaSyAPyF5GfQI-kOa6nZwO8EsNrGdEx9bioNs` | tombulled `:106` |
| ANDROID_KIDS | `AIzaSyAxxQKWYcEX8jHlflLt2Qcbb-rlolzBhhk` | tombulled `:119` |
| IOS_KIDS | `AIzaSyA6_JWXwHaVBQnoutCv1-GvV97-rJ949Bc` | tombulled `:126` |
| WEB_KIDS | `AIzaSyBbZV_fZ3an51sF-mvs5w37OqqbsTOzwtU` | tombulled `:354` |
| WEB_CREATOR (отд.) | `AIzaSyBUPetSUmoZL-OhlxA7wSac5XinrygCqMo` | tombulled `:271` |

> Где брать ключ из «живой» страницы: он лежит в `ytcfg.set({...})` / `yt.setConfig(...)` внутри
> HTML главной или embed-страницы (поле `INNERTUBE_API_KEY`), рядом с `INNERTUBE_CONTEXT`,
> `VISITOR_DATA`, `STS`.

---

## 4. Анатомия запроса (context, client, payload, заголовки) + готовые скелеты

### 4.1 Структура тела
```jsonc
{
  "context": {
    "client": {
      "clientName": "WEB",            // строка; числовой id идёт в заголовок X-YouTube-Client-Name
      "clientVersion": "2.20260114.08.00",
      "hl": "en", "gl": "US",
      // мобильные/TV клиенты также шлют:
      "deviceMake": "Apple", "deviceModel": "iPhone16,2",
      "osName": "iOS", "osVersion": "18.0",
      "androidSdkVersion": 33,        // только ANDROID*
      "platform": "MOBILE",           // MOBILE | TV | DESKTOP
      "visitorData": "<base64url protobuf, опционально>"
    },
    "user":    { "lockedSafetyMode": false },
    "request": { "useSsl": true, "internalExperimentFlags": [] },
    "thirdParty": { "embedUrl": "https://www.youtube.com/" }  // для embed/age-gate
  },
  // поля под endpoint:
  "videoId": "…",            // player/next
  "browseId": "…",           // browse
  "query": "…", "params": "…", // search
  "continuation": "…"        // пагинация
}
```
Минимально жизнеспособный `context` (то, что реально шлют tombulled/innertube и Invidious) — это
`client.{clientName, clientVersion, hl, gl}`. Всё остальное опционально.

**Wire-схема (protobuf, из YouTube.js `protos/`):** `InnerTubeContext { client=1, user=3,
capabilities=4, request=5, clickTracking=6, thirdParty=7, adSignals=9 }`. `ClientInfo`:
`hl=1, gl=2, visitorData=14, userAgent=15, clientName=16 (int32 id), clientVersion=17, osName=18,
osVersion=19, deviceMake=12, deviceModel=13, platform=42, androidSdkVersion=64`.

### 4.2 Заголовки
- `Content-Type: application/json` (или `application/x-protobuf` для protobuf-клиентов).
- `X-YouTube-Client-Name: <числовой id>` и `X-YouTube-Client-Version: <clientVersion>`.
- `X-Goog-Visitor-Id: <visitorData>` — когда он есть.
- `User-Agent:` — **должен соответствовать клиенту** (особенно ANDROID/IOS — см. §5).
- `X-Goog-Api-Format-Version: 1` (mobile/protobuf; tombulled шлёт `1`) или `2`.
- `Origin: https://www.youtube.com`, `Referer: https://www.youtube.com/` — для WEB/MWEB.
- `Cookie: SOCS=CAISAiAD` — consent cookie, чтобы обойти EU consent-стену (MeeTube это использует).
- Авторизованные: `Authorization:` (Bearer **или** SAPISIDHASH — §6), `X-Goog-AuthUser: 0`,
  `X-Goog-PageId`, `X-Origin`, `X-Youtube-Bootstrap-Logged-In: true`.

### 4.3 Опаковые `params` / `continuation`
Многие поля (`search.params`, `browse.params` вкладок канала, токены `continuation`) —
**base64url-protobuf**. Известные константы (можно хардкодить):
- поисковые фильтры: `EgIQAQ==` (видео), `EgIQAg==` (каналы), `EgIQAw==` (плейлисты);
- вкладка «Видео» канала: `EgZ2aWRlb3PyBgQKAjoA`.

`continuation`-токены обычно **читаются из предыдущего ответа**, а не конструируются. Декодировать/
строить можно через `protoc --decode_raw` (схемы — davidzeng0/innertube).

### 4.4 Готовые скелеты запросов

**`/player` — аноним, клиент IOS, без дешифровки → `hlsManifestUrl` (рекомендуется тонкому клиенту)**
```bash
curl 'https://www.youtube.com/youtubei/v1/player?prettyPrint=false' \
  -H 'Content-Type: application/json' \
  -H 'X-YouTube-Client-Name: 5' \
  -H 'X-YouTube-Client-Version: 20.49.6' \
  -H 'User-Agent: com.google.ios.youtube/20.49.6 (iPhone16,2; U; CPU iOS 18_0 like Mac OS X)' \
  --data-raw '{"context":{"client":{"clientName":"IOS","clientVersion":"20.49.6","deviceMake":"Apple","deviceModel":"iPhone16,2","osName":"iOS","osVersion":"18.0","hl":"en","gl":"US"}},"videoId":"VIDEO_ID","contentCheckOk":true,"racyCheckOk":true}'
# → .streamingData.hlsManifestUrl (играть напрямую) либо .streamingData.formats[].url
```

**`/search` (WEB, фильтр = видео)**
```bash
curl 'https://www.youtube.com/youtubei/v1/search?prettyPrint=false' \
  -H 'Content-Type: application/json' \
  -H 'X-YouTube-Client-Name: 1' -H 'X-YouTube-Client-Version: 2.20260114.08.00' \
  --data-raw '{"context":{"client":{"clientName":"WEB","clientVersion":"2.20260114.08.00","hl":"en","gl":"US"}},"query":"lo-fi","params":"EgIQAQ=="}'
# walk contents.twoColumnSearchResultsRenderer…itemSectionRenderer.contents[].videoRenderer
```

**`/browse` (главная `FEwhat_to_watch`; вкладка «Видео» канала; плейлист)**
```bash
curl 'https://www.youtube.com/youtubei/v1/browse?prettyPrint=false' \
  -H 'Content-Type: application/json' -H 'X-YouTube-Client-Name: 1' -H 'X-YouTube-Client-Version: 2.20260114.08.00' \
  --data-raw '{"context":{"client":{"clientName":"WEB","clientVersion":"2.20260114.08.00","hl":"en","gl":"US"}},"browseId":"FEwhat_to_watch"}'
# канал «Видео»: {"browseId":"UCxxxx","params":"EgZ2aWRlb3PyBgQKAjoA"} | плейлист: {"browseId":"VLPLxxxx"}
```

**`/next` (страница просмотра → связанные + continuation-токен комментариев)**
```bash
curl 'https://www.youtube.com/youtubei/v1/next?prettyPrint=false' \
  -H 'Content-Type: application/json' -H 'X-YouTube-Client-Name: 1' -H 'X-YouTube-Client-Version: 2.20260114.08.00' \
  --data-raw '{"context":{"client":{"clientName":"WEB","clientVersion":"2.20260114.08.00","hl":"en","gl":"US"}},"videoId":"VIDEO_ID"}'
# пагинация: повторный POST на ТОТ ЖЕ endpoint с {"context":{…},"continuation":"<token>"}
```

**Универсальный POST на Python (ключ не нужен)**
```python
import requests
HOST='https://www.youtube.com'
CTX={'client':{'clientName':'WEB','clientVersion':'2.20260114.08.00','hl':'en','gl':'US'}}
HEADERS={'Content-Type':'application/json','X-YouTube-Client-Name':'1',
         'X-YouTube-Client-Version':'2.20260114.08.00'}

def innertube(endpoint, **fields):
    body={'context':CTX, **fields}
    r=requests.post(f'{HOST}/youtubei/v1/{endpoint}?prettyPrint=false',
                    headers=HEADERS, json=body, timeout=20)
    r.raise_for_status(); return r.json()

player = innertube('player', videoId='VIDEO_ID', contentCheckOk=True, racyCheckOk=True)
search = innertube('search', query='lo-fi', params='EgIQAQ==')
home   = innertube('browse', browseId='FEwhat_to_watch')
nxt    = innertube('next',   videoId='VIDEO_ID')
print(player['playabilityStatus']['status'])
```

**Универсальный POST на JS (`fetch`)**
```js
async function innertube(endpoint, fields) {
  const ctx = { client: { clientName: 'WEB', clientVersion: '2.20260114.08.00', hl: 'en', gl: 'US' } };
  const res = await fetch('https://www.youtube.com/youtubei/v1/' + endpoint + '?prettyPrint=false', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json',
      'X-YouTube-Client-Name': '1', 'X-YouTube-Client-Version': '2.20260114.08.00' },
    body: JSON.stringify(Object.assign({ context: ctx }, fields))
  });
  return res.json();
}
// await innertube('player', { videoId:'VIDEO_ID', contentCheckOk:true, racyCheckOk:true });
```

---

## 5. Client types (таблица + разбор)

`context.client.clientName` — **строка** (`"WEB"`, `"ANDROID"`…); парный числовой **client-id** идёт в
заголовок `X-YouTube-Client-Name`. Версии ниже — **снимок на янв–июн 2026** (yt-dlp / NewPipe /
YouTube.js / Dmitry WP); **перед продакшеном брать актуальные из yt-dlp `_base.py`** — устаревшие
веб-версии «мягко» блокируются.

### 5.1 Сводная таблица
`PO` = нужен ли PO token для GVS-стримов; `JS` = нужна ли дешифровка сигнатуры (player JS).

| clientName | id | clientVersion (≈2026) | PO token? | JS-sig? | Что даёт / подводные камни |
|---|---|---|---|---|---|
| **WEB** | 1 | `2.20260623.01.00` (YT.js) / `2.20260114.08.00` (yt-dlp) | **да** | **да** | Полный веб; нужен и decipher, и po_token. Не для JS-less. |
| WEB (Safari UA) | 1 | то же | HLS:нет/DASH:да | да | Safari-UA отдаёт пред-смерженный HLS; дефолт yt-dlp. |
| **WEB_EMBEDDED_PLAYER** | 56 | `1.20260206.01.00` | нет | да | Embeddable; `thirdParty.embedUrl`; частичный обход age-gate. |
| **WEB_REMIX** (Music) | 67 | `1.20250219.01.00` | да | да | Хост `music.youtube.com`; ключ `AIzaSy…DNX30`. |
| WEB_CREATOR | 62 | `1.20241203.01.00` | да | да | **Требует auth**. |
| **MWEB** | 2 | `2.20260205.04.01` | да | да | Мобильный веб. |
| **ANDROID** | 3 | `21.03.36` (YT.js) / `20.10.38` (Dmitry) | да* | **НЕТ** | Возвращает plain `url`; **отвергает Bearer (400)** → звать анонимно. |
| **ANDROID_VR** | 28 | `1.65.10` … `1.71.26` | **нет** | **НЕТ** | **Дефолт yt-dlp**; нет po_token, нет JS. ⚠ `>1.65` может форсить SABR; нестабилен с ~2026-03. |
| **IOS** | 5 | `20.49.6` … `21.03.2` | да* | **НЕТ** | Возвращает **`hlsManifestUrl`** (server-signed) + plain progressive. Основной плеер Dmitry WP. |
| **TVHTML5** | 7 | `7.20260311.12.00` (YT.js) / `7.20250209.19.00` (Dmitry) | нет* | частично | Living-room; активно для authed browse. Без cookies форматы DRM'd. |
| **TVHTML5_SIMPLY_EMBEDDED_PLAYER** | 85 | `2.0` | varies | varies | ⚠️ **Бывший** обход age-gate; **снят ~янв 2026** (требует sign-in на каждое видео; удалён в yt-dlp #15787). Подтверждено верификацией — см. §6.5/§13. |
| TVHTML5_SIMPLY | 75 (yt-dlp/tombulled) / 74 (Invidious) | `1.0` | да | да | См. конфликт id ниже. |
| ANDROID_MUSIC | 21 | `5.34.51` | — | — | YT Music на Android. |
| WEB_KIDS | 76 | `2.20260205.00.00` | — | — | YouTube Kids веб. |
| ANDROID_CREATOR | 14 | `22.43.101` | — | — | Studio на Android. |

> **Конфликт id, который надо держать в голове:** `TVHTML5_SIMPLY` — **id 75** по yt-dlp/tombulled,
> но **id 74** по Invidious. Источники реально расходятся — не нормализуйте молча.

Полная карта числовых id (zerodytrash + tombulled `config.py`) — ~56–74 клиента; tombulled перечисляет
56 контекстов (`WEB, MWEB, ANDROID, IOS, TVHTML5, TVLITE, ANDROID_CREATOR, IOS_CREATOR, ANDROID_KIDS,
IOS_KIDS, ANDROID_MUSIC, IOS_MUSIC, ANDROID_VR, ANDROID_UNPLUGGED, WEB_REMIX, WEB_KIDS,
TVHTML5_SIMPLY, TVHTML5_SIMPLY_EMBEDDED_PLAYER, …`).

### 5.2 «Без PoToken и без JS-дешифровки» — критично для legacy
Набор клиентов, которые **сегодня** отдают готовые/нешифрованные стрим-URL без player JS и без
po_token (по состоянию на середину 2026, `FRAGILE`):
- **`ANDROID_VR`** (28) — главный кандидат: `REQUIRE_JS_PLAYER=false`, без po_token, plain `formats[].url`.
- **`IOS`** (5) — отдаёт `hlsManifestUrl` (server-signed HLS, готов к проигрыванию).
- **`ANDROID`** (3) — plain progressive `url` (но `&n=`-throttle всё ещё применяется, см. §7).

⚠️ Это **самая эродирующая ось** всего документа: надёжность ANDROID/IOS падала весь 2024–2025; статус
«верен на дату чтения», не контракт. **Единственный авторитетный источник — живая таблица
`INNERTUBE_CLIENTS` + `*PoTokenPolicy` в yt-dlp**, меняется от коммита к коммиту.

---

## 6. Аутентификация и защита

### 6.1 Три режима доступа
1. **Аноним** (по умолчанию). Работает для player/search/browse/next/comments. Опционально слать
   `visitorData` (эхо `responseContext.visitorData` из первого ответа; либо сгенерировать локально).
2. **Cookie + SAPISIDHASH** (web). Из cookies логина Google (`SAPISID`/`__Secure-3PAPISID`):
   `Authorization: SAPISIDHASH <ts>_<sha1("<ts> <SAPISID> https://www.youtube.com")>` (+ варианты
   `SAPISID1PHASH`/`SAPISID3PHASH`), плюс `X-Goog-AuthUser`, `X-Goog-PageId`, `X-Origin`.
   ```python
   import hashlib, time
   origin='https://www.youtube.com'; ts=str(int(time.time()))
   def sidhash(sid): return hashlib.sha1(f'{ts} {sid} {origin}'.encode()).hexdigest()
   auth=f'SAPISIDHASH {ts}_{sidhash(SAPISID)}'
   ```
3. **OAuth 2.0 TV «limited-input device»** — headless/app-логин без cookie jar (его используют Dmitry
   WP, YouTube.js, yt-dlp):
   - `POST https://www.youtube.com/o/oauth2/device/code` (или `https://oauth2.googleapis.com/device/code`)
     с `client_id`, `scope`, `device_id`, `device_model=ytlr:…` → `device_code`, `user_code`,
     `verification_url`, `interval`.
   - Поллинг `POST https://oauth2.googleapis.com/token`,
     `grant_type=urn:ietf:params:oauth:grant-type:device_code` (исторически также
     `http://oauth.net/grant_type/device/1.0`), пока пользователь не авторизует.
   - Хранить **только `refresh_token`**; access-токены минтить на лету
     (`grant_type=refresh_token`).
   - Слать `Authorization: Bearer <access_token>` на персонализированных вызовах
     (subscriptions/history/like/subscribe/accounts_list).
   - **Публичные TV-креды** (Dmitry WP, byte-identical с docs/INNERTUBE_API.md и YouTube.js):
     - `client_id = 861556708454-d6dlm3lh05idd8npek18k6be8ba3oc68.apps.googleusercontent.com`
     - `client_secret = SboVhoG9s0rNafixCSGGKXAT`
     - scope `http://gdata.youtube.com https://www.googleapis.com/auth/youtube-paid-content`
   - ⚠ Креды могут ротироваться — YouTube.js/rustypipe скрейпят их из TV `base.js`. `FRAGILE`.

> ⚠️ **Критическая ловушка (подтверждена в коде Dmitry WP):** ANDROID/IOS `player` **отвергает Bearer
> с `400 INVALID_ARGUMENT`**. Всегда зовите `player` **анонимно**, даже залогиненным (Dmitry зовёт
> ANDROID-anon → WEB-anon → WEB+Bearer). Bearer — только на browse/next/like/subscribe через
> WEB/TVHTML5/MWEB.
>
> ⚠️ **Жёсткое ограничение:** во всех изученных проектах **нет рабочего рецепта авторизованного
> `player`**, отдающего персональные/премиум-стримы через app-клиенты. Персонализация навешивается на
> `browse`/`next`; воспроизведение фактически всегда анонимно.

### 6.2 `visitorData`
Base64url-protobuf `{ id:1 (string, ~11 случайных символов), timestamp:5 (int32) }`. Связывает
анонимную сессию. Можно сгенерировать локально (так делает YouTube.js: `encodeVisitorData(randomId,
ts)`), но **локально сгенерированный может не привязываться к po_token** — предпочтительнее
server-issued (из `responseContext`/`visitor_id`). У WEB-клиентов есть статичный fallback
`STATIC_VISITOR_ID = '6zpwvWUNAco'` (YouTube.js `Constants.ts`). Dmitry WP хардкодит конкретный
`visitorData`-блоб (см. §12).

### 6.3 PoToken / BotGuard (слой 2024–2026) — `FRAGILE`
YouTube гейтит часть запросов **Proof-of-Origin Token (`po_token`)** — аттестацией от **BotGuard**
(Web) / **DroidGuard** (Android) / **iOSGuard** (iOS). Без него флагнутые IP получают
*«Sign in to confirm you're not a bot»*.

- **Три контекста** (yt-dlp): **GVS** (URL googlevideo, как `&pot=`), **PLAYER** (в теле `player`,
  `serviceIntegrityDimensions.poToken`), **SUBS** (субтитры).
- **Привязка:** session-bound (`visitorData`/`dataSyncId`) или content-bound (`videoId`, per-`player`,
  не кэшировать).
- **Матрица по клиентам (yt-dlp PO Token Guide, wiki ~2026-03):**

  | client | GVS-PoToken | Player-PoToken | примечание |
  |---|---|---|---|
  | web / mweb / web_creator / web_music | **да** | нет | SABR-only форматы; web_creator нужны cookies |
  | android / ios | да* | да* | нет cookie; `*` = either/or |
  | **tv** (TVHTML5) | **нет** | нет | форматы DRM'd без cookies |
  | **web_embedded** | **нет** | нет | только embeddable видео |
  | **android_vr** | **нет** | **нет** | дефолт yt-dlp; без JS-плеера |
  | tv_simply (75) | да | — | без cookies |

  Premium-подписчики освобождены от GVS-токенов. Токены не cross-usable между BotGuard/DroidGuard/
  iOSGuard. Жизнь токена — от ~часов до месяцев; отсутствие → HTTP 403.
- **Как минтится** (LuanRT/BgUtils — эталон):
  1. `POST https://jnn-pa.googleapis.com/$rpc/google.internal.waa.v1.Waa/Create`
     (header `x-goog-api-key: AIzaSyDyT5W0Jh49F30Pqqtyfdf7pDLFKLJoAnw`, body `["<requestKey>"]`,
     известный `requestKey "O43z0dpjhgX20SCx4KAo"`) → обфусцированный **BotGuard VM** + программа.
  2. Запустить VM → `botguardResponse`.
  3. `…/Waa/GenerateIT` с `["<requestKey>", "<botguardResponse>"]` → `[integrityToken,
     estimatedTtlSecs, mintRefreshThreshold, websafeFallbackToken]`.
  4. `WebPoMinter` биндит identifier (visitorData/dataSyncId/videoId) → финальный токен.
  - Альт-путь: challenge через InnerTube `att/get`.
- **Провайдеры:** `bgutil-ytdlp-pot-provider` (HTTP/Docker), `rustypipe-botguard`, `pytubefix`
  (`botGuard.js` под Node). Всем нужен **JS-рантайм + DOM**. Токены **IP/ASN-bound** — минтить на том
  же egress, с которого стримите.
- **Итог для тонкого/legacy-клиента:** *не реализуйте BotGuard.* Используйте `ANDROID_VR`/`IOS`/
  `ANDROID`, которым po_token сейчас не нужен (§5, §11).

### 6.4 Дешифровка сигнатуры и `n`-параметра — `FRAGILE`
Два независимых JS-челленджа (оба в `base.js`):
1. **`s` (signature)** — только web/tv; URL приходит в `signatureCipher` (`url=…&s=<sig>&sp=sig`);
   `s` надо прогнать через функцию `decipher` и добавить как `sp`.
2. **`n` (throttling)** — на **почти всех** стрим-URL (`&n=…`). Без трансформации скорость режется до
   ~50 КБ/с. yt-dlp/NewPipe/YouTube.js извлекают `nsig` из `base.js` и исполняют.
3. **`signatureTimestamp` (sts)** — 5-значный int из `base.js`, шлётся как
   `playbackContext.contentPlaybackContext.signatureTimestamp`, чтобы сервер вернул шифры под версию
   плеера. **Нужен только когда вы дешифруете** (web/tv).

> ⚠️ **С 2025.11.12 (yt-dlp #15012) для web-клиентов требуется ВНЕШНИЙ JS-рантайм** (Deno/Node/Bun/
> QuickJS) — встроенного интерпретатора yt-dlp больше не хватает для nsig. Это усиливает довод
> «выбирайте клиент без дешифровки» для legacy. `FRAGILE` — перепроверить.

**Как исполняют JS разные проекты:** yt-dlp → внешний рантайм; NewPipe → Rhino; YouTube.js → AST-
извлечение (meriyah) + sandbox `eval`; Dmitry WP → **UWP WebView `eval`** извлечённой `nsig` (но для
основного пути Dmitry предпочитает IOS HLS, где дешифровка не нужна).

### 6.5 Age-gate (обход)
- 🔴 **Классический embed-обход БОЛЬШЕ НЕ РАБОТАЕТ (подтверждено состязательной проверкой 3/3).**
  Приём `clientName: "TVHTML5_SIMPLY_EMBEDDED_PLAYER", clientVersion: "2.0"` + `thirdParty.embedUrl`
  + `signatureTimestamp` (классика tyrrrz) **снят ~янв 2026**: YouTube требует sign-in на каждое
  видео для этого клиента; yt-dlp удалил его в **PR #15787 / commit `8eb7943`** (merged 2026-01-31,
  *«This client now requires sign-in for every video»*). Параметры (id 85, v2.0) исторически верны, но
  как анонимный age-gate-обход **мертвы**.
- **Частичный выживший:** `WEB_EMBEDDED_PLAYER` (id 56) обходит age-restriction **только иногда**
  (embeddable-видео); на остальном возвращает `UNPLAYABLE`.
- **Что работает сейчас:** age-restricted контент требует **авторизации** — клиент `web_creator`/`tv`
  **+ PO token + cookies/аккаунт**. Иными словами, для тонкого/анонимного клиента age-gated видео в
  2026 практически недоступно без логина. `FRAGILE` — сверять с живым yt-dlp.
- Флаги `contentCheckOk:true` + `racyCheckOk:true` в теле `player` — для «racy»/sensitive контента
  (Dmitry WP их всегда шлёт); это **не** age-gate-обход, а согласие на «деликатный» контент.

### 6.6 X-Goog-Device-Auth (нативные iOS/Android) — справочно
Нативные клиенты подписывают запросы заголовком `X-Goog-Device-Auth` (gist leptos-null
`YTApiaryDeviceCrypto`): одноразовая регистрация устройства
(`POST …/deviceregistration/v1/devices`) → `deviceID` + зашифрованный `deviceKey` (AES-CTR с
хардкод-ключом проекта); затем per-request `data`/`content` = усечённые HMAC-SHA1 от URL/тела.
Для сторонних клиентов обычно **не нужен** (хватает `context.client` + UA). Справочно для понимания
нативного трафика.

### 6.7 Rate limits / анти-бот
Формальной квоты нет, но: datacenter-IP быстро ловят bot-wall; residential/mobile анонимно работают;
вежливый rate-limit и переиспользование одного `visitorData` на сессию снижают флаги. Отсутствие
нужного po_token → `403`.

---

## 7. Формат ответа и парсинг (renderer-модель, continuation, streamingData)

### 7.1 Renderer-модель
Ответы — деревья **renderer**-объектов; обходишь по именам ключей.
- **Списки/ленты:** `sectionListRenderer → itemSectionRenderer → contents[]` с `videoRenderer`,
  `gridVideoRenderer`, `compactVideoRenderer` (сайдбар), `playlistVideoRenderer`,
  `richItemRenderer.content.videoRenderer` (главная), `playlistRenderer`, `channelRenderer`,
  `lockupViewModel`/`shortsLockupViewModel` (новые), `reelItemRenderer`, `tileRenderer` (TV).
- **Текст:** либо `{"simpleText":"…"}`, либо `{"runs":[{"text":"…"},…]}` — конкатенировать `runs`.
- **Страница просмотра (`next`):** `contents.twoColumnWatchNextResults` →
  `results.results.contents[]` с `videoPrimaryInfoRenderer` (title, viewCount),
  `videoSecondaryInfoRenderer.owner.videoOwnerRenderer` (канал,
  `navigationEndpoint.browseEndpoint.browseId`); связанные =
  `secondaryResults.secondaryVideoResults.contents[].compactVideoRenderer`.
- **Комментарии (текущий формат):** `engagementPanels[] → engagementPanelSectionListRenderer`
  (`panelIdentifier == "engagement-panel-comments-section"`) → найти
  `continuationItemRenderer.continuationEndpoint.continuationCommand.token`, POST его в `next`, затем
  обходить **`commentEntityPayload`** (`author.displayName`, `properties.content`,
  `avatar.image.sources`). Старый `commentRenderer` тоже встречается.
- **Диагностика плеера:** `playabilityStatus.status` (`OK`/`LOGIN_REQUIRED`/
  `AGE_VERIFICATION_REQUIRED`/`UNPLAYABLE`/`LIVE_STREAM_OFFLINE`) + `.reason`.

### 7.2 Continuation (пагинация)
```
1. В ответе найти: continuationItemRenderer.continuationEndpoint.continuationCommand.token
   (legacy: nextContinuationData.continuation / reloadContinuationData.continuation)
2. Повторный POST на ТОТ ЖЕ endpoint:  {"context":{…}, "continuation":"<token>"}
3. Новые элементы — под onResponseReceivedActions[].appendContinuationItemsAction.continuationItems
   (или …Endpoints[].reloadContinuationItemsCommand)
```
Токены — base64url-protobuf; передаются **дословно**, обычно не конструируются вручную.

### 7.3 streamingData (стримы)
`player.streamingData`:
- **`formats`** — прогрессивные (muxed A+V): itag 18 (360p mp4) / 22 (720p).
- **`adaptiveFormats`** — раздельные video-only / audio-only (DASH-style): H.264/VP9/AV1/Opus, с
  `bitrate`, `width/height`, `initRange`, `indexRange`, `contentLength`.
- **`hlsManifestUrl`** / **`dashManifestUrl`** — готовые манифесты.

URL каждого формата — одно из:
- **plain `url`** — играбелен сразу (клиенты ANDROID/IOS/ANDROID_VR).
- **`signatureCipher`** (web/tv) — `url=…&s=<sig>&sp=sig`, нужна дешифровка (§6.4).

**Escape-hatch (ключевая идея):** клиент **IOS** → `hlsManifestUrl` (готовый HLS); клиенты
**ANDROID/ANDROID_VR** → plain progressive `url`. ⚠ Оговорка: HLS/прогрессив могут нести `&n=`; если
воспроизведение тротлится — `n` всё равно надо решать (проверить на реальном устройстве, см. §11).

### 7.4 Субтитры/транскрипт (низкофрикционный путь — без PoToken, без сигнатуры)
```
baseUrl = response.captions.playerCaptionsTracklistRenderer.captionTracks[0].baseUrl
GET {baseUrl}&fmt=json3      # таймкодированные события; без auth/key/PoToken (подтверждено 2026-01)
```
Либо endpoint `get_transcript` с `params` (base64-protobuf videoId+lang).

### 7.5 Пример ответа `/player` (ключевые поля, усечённо)
```jsonc
{
  "playabilityStatus": { "status": "OK" },
  "streamingData": {
    "formats": [
      { "itag": 18, "mimeType": "video/mp4; codecs=\"avc1.42001E, mp4a.40.2\"",
        "url": "https://rr3---sn-….googlevideo.com/videoplayback?…&n=…" }
    ],
    "adaptiveFormats": [ { "itag": 137, "mimeType": "video/mp4; codecs=\"avc1.640028\"",
        "bitrate": 4000000, "width": 1920, "height": 1080,
        "url": "https://…/videoplayback?…" } ],
    "hlsManifestUrl": "https://manifest.googlevideo.com/api/manifest/hls_variant/…/index.m3u8"
  },
  "videoDetails": { "videoId": "VIDEO_ID", "title": "…", "lengthSeconds": "213",
                    "author": "…", "channelId": "UC…", "viewCount": "123456" },
  "captions": { "playerCaptionsTracklistRenderer": { "captionTracks": [
      { "baseUrl": "https://www.youtube.com/api/timedtext?…", "languageCode": "en" } ] } }
}
```

---

## 8. Endpoints в деталях

### `player`
- **URL:** `POST …/youtubei/v1/player?prettyPrint=false`
- **Тело:** `videoId`, `contentCheckOk:true`, `racyCheckOk:true`,
  опц. `playbackContext.contentPlaybackContext.signatureTimestamp`,
  опц. `serviceIntegrityDimensions.poToken`, опц. `params`.
- **Ответ:** `streamingData`, `videoDetails`, `playabilityStatus`, `captions`, `microformat`.
- **Пример:** см. §4.4 (IOS-аноним → `hlsManifestUrl`).

### `search`
- **URL:** `POST …/search?prettyPrint=false`. **Тело:** `query`, опц. `params` (фильтр), `continuation`.
- **Ответ:** `contents.twoColumnSearchResultsRenderer…itemSectionRenderer.contents[].videoRenderer`.

### `browse`
- **URL:** `POST …/browse`. **Тело:** `browseId` (`FEwhat_to_watch`/`FEsubscriptions`/`UC…`/`VL…`),
  опц. `params` (вкладка), `continuation`.
- **Ответ:** для канала — `twoColumnBrowseResultsRenderer.tabs[]`; для ленты —
  `richGridRenderer.contents[].richItemRenderer.content.videoRenderer`.

### `next`
- **URL:** `POST …/next`. **Тело:** `videoId` (+ `playlistId`/`params`/`continuation`).
- **Ответ:** `twoColumnWatchNextResults` (метаданные + связанные + точка входа комментариев).
- **Комментарии:** `next(videoId)` → токен панели комментариев → `next(continuation)` →
  `commentEntityPayload`.

### `navigation/resolve_url`
- **Тело:** `url` (vanity/@handle) → `…endpoint.browseEndpoint.browseId`.

### `guide` / `config` / `account/accounts_list`
- `guide` — меню/подписки; `config` — bootstrap (visitorData/флаги); `account/accounts_list` —
  список аккаунтов (authed, `accountReadMask`).

### `get_transcript`
- **Тело:** `params` (base64-protobuf videoId+lang) → события субтитров. См. §7.4.

### `music/get_search_suggestions`, `music/get_queue`
- **Хост:** `music.youtube.com`, клиент `WEB_REMIX`. Тело: `input` / (`videoIds`,`playlistId`).
- Полнее — `sigma67/ytmusicapi` (стандарт по Music-endpoints).

### Studio (creator) — **частично, требует проверки**
- Хост `studio.youtube.com`, клиент `WEB_CREATOR`/`ANDROID_CREATOR` (реферал `studio.youtube.com`,
  ключ `AIzaSy…CqMo`). Доступ требует авторизации (Bearer/cookies).
- Конкретные creator-специфичные пути (`/creator/*`, аналитика, управление видео) в изученном корпусе
  **не перечислены** — **«не найдено / требует проверки»**; ни один из локальных проектов и
  опрошенных open-source клиентов их не реализует (фокус сообщества — потребление, не Studio).

### `live_chat/get_live_chat`
- **Тело:** `continuation` → действия чата (`addChatItemAction…`). Поллинг по новому continuation.

### `att/get`
- **Тело:** `engagementType` → challenge аттестации BotGuard (вход в PoToken-флоу, §6.3).

### Авторизованные действия (Bearer через TV-OAuth, контекст WEB/TVHTML5)
```bash
POST …/youtubei/v1/subscription/subscribe        # Authorization: Bearer <token>
{"context":{…WEB/TVHTML5…},"channelIds":["UCxxxx"]}
# like/like · like/dislike — {"target":{"videoId":"…"}}
```

---

## 9. Библиотеки по языкам

| Проект | Язык | Что умеет | Активность / заметки |
|---|---|---|---|
| **LuanRT/YouTube.js** (`youtubei.js`) | TS/JS | Самый полный: session/context, OAuth TV, полный decipher (sig+nsig), SABR/OTF/DASH, renderer-парсер, 14+ клиентов | ~5k★, MIT, активен; v17.2.0 (2026-06-23). **Эталон нюансов.** Питает FreeTube Local API |
| **yt-dlp** (`yt_dlp/extractor/youtube/`) | Python | **Авторитет** по текущим версиям клиентов, политике po_token, sig/nsig; даунлоадер | Обновляется почти ежедневно. **Старт за текущими константами** |
| **TeamNewPipe/NewPipeExtractor** | Java | Промышленный парсинг renderer/continuation, sig/nsig-регэкспы, `PoTokenProvider`, DASH-синтез | Активен; питает NewPipe, Tubular, Piped |
| **tombulled/innertube** | Python | Чистейшая модель «endpoint + context»; таблица 56 клиентов с ключами/UA | v2.1.19 (2025-07-01), ~24 релиза. **OAuth НЕ реализован** (аноним) |
| **sigma67/ytmusicapi** | Python | YT Music (`WEB_REMIX`) endpoints + auth | Активен; стандарт для Music |
| **LuanRT/BgUtils** (`bgutils-js`) | TS | **Эталон** po_token/BotGuard (WAA Create/GenerateIT, биндинг токена) | v3.2.0; нужен JS+DOM |
| **Tyrrrz/YoutubeExplode** | .NET/C# | Метаданные + стримы + дешифровка; чистый высокоуровневый API | Активен; релевантен для Windows/WP-стека |
| **kkdai/youtube** | Go | Метаданные + загрузка (CLI `youtubedr`) | Активен |
| **ThetaDev/rustypipe** (+ `rustypipe-botguard`) | Rust | NewPipe-вдохновлённый клиент; po_token отдельным CLI | Codeberg (+ зеркала); web-клиентам нужны токены с 2024-08 |
| **JuanBindez/pytubefix** | Python | Активно поддерживаемый форк pytube; po_token через `botGuard.js` (Node) | Активен (pytube «застрял») |
| **pytube** (`innertube.py`) | Python | Лаконичная база: base URL, context, OAuth, continuation, bypass age | Стагнирует; см. pytubefix |
| **haxzie/innerTube.js** | JS | Обёртка + пример формата ответа `search` | Демонстрационный |
| **zerodytrash/YouTube-Internal-Clients** | Python | Brute-force скрытых клиентов; источник карты `clientName`→id | Эталон перечня id |
| **davidzeng0/innertube** | proto/MD | `.proto`-схемы + декод base64-protobuf `params`/`continuation` + UMP | Документация схем |
| **MohammadKobirShah/InnerTube-API** | Python (FastAPI) | REST-обёртка над InnerTube — карта endpoints/фич | Демонстрационный |
| **Benjamin-Loison/YouTube-operational-API** | PHP | Метаданные без дешифровки — доказывает «дешёвый путь» | Активен |
| Hexer10/youtube_explode_dart, z-huang/InnerTune, ViMusic/RiMusic | Dart/Kotlin | Порты/Music-клиенты | — |

---

## 10. Проекты-потребители

**Прямые клиенты InnerTube** (сами ходят в `/youtubei`):
- **NewPipe / NewPipeExtractor** (Java, клиенты ANDROID+WEB); форк **Tubular**.
- **youtubei.js / YouTube.js** (JS) → питает **FreeTube** «Local API».
- **SmartTube** (Android TV).
- **InnerTune / OuterTune / ViMusic / RiMusic** (YT Music, Android).
- **Rehike** + hitchhiker (PHP, восстановление UI 2013 «Hitchhiker»).

**Прокси/серверные фронтенды** (свой API, **НЕ** сам InnerTube):
- **Invidious** (Crystal) — скрейп + внутренние endpoints, отдаёт **свой** Invidious API
  (+ `invidious-companion` выносит player/po_token/sig). ⚠ **Это не InnerTube** — это отдельный прокси.
- **Piped** (Vue + Java) — Java-backend использует **NewPipeExtractor** (= InnerTube) под капотом.
- **Hyperpipe** (YT Music поверх Piped) — заброшен.

**Клиенты поверх прокси:**
- **FreeTube** → Local API (InnerTube через youtubei.js) **или** Invidious API.
- **LibreTube** → backend **Piped** (по умолчанию) или локальная экстракция.
- **Clipious** → инстансы Invidious.
- Дальше: FluxTube, Yattee, PlasmaTube, Materialious, PipePipe — фронтенды/форки.

> Чёткое различие: **InnerTube** = приватный backend Google. **Invidious/Piped** = сторонние прокси
> со своим API. Библиотеки (NewPipeExtractor, youtubei.js) = прямые клиенты InnerTube.

---

## 11. Legacy / ограниченные платформы (Windows Phone, embedded, Nokia N9)

Контекст: старый .NET/Silverlight/UWP, отсутствие современного JS-движка для дешифровки, ограниченный
HTTP-стек. Цель — заставить InnerTube работать **без** BotGuard и **без** дешифровки сигнатур.

### 11.1 Что работает без PoToken/дешифровки (на середину 2026, `FRAGILE`)
- **Метаданные** (search/browse/next/comments) — аноним WEB/ANDROID/TVHTML5: надёжно, без токенов.
- **Стримы без дешифровки:**
  - **IOS** → `hlsManifestUrl` (server-signed HLS, готов к проигрыванию) — *простейший путь;
    основной у Dmitry WP.*
  - **ANDROID / ANDROID_VR** → plain progressive `url` (но `&n=`-throttle всё ещё применяется →
    медленнее, если не трансформировать `n`).
- **Субтитры** через `captionTracks[].baseUrl&fmt=json3` — без auth/key/PoToken.

### 11.2 Минимальный жизнеспособный запрос
Наименьший набор заголовков, который ещё возвращает метаданные + субтитры (без PoToken/сигнатуры):
```http
POST https://www.youtube.com/youtubei/v1/player?prettyPrint=false
Content-Type: application/json
User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36

{"context":{"client":{"clientName":"WEB","clientVersion":"2.20240101.00.00","hl":"en","gl":"US"}},
 "videoId":"VIDEO_ID"}
```
> С WEB это в 2026 вернёт SABR-only/без стрим-URL для медиа без GVS PoToken, **но** надёжно отдаёт
> метаданные + `captions…baseUrl` без PoToken. Для **медиа** на legacy — берите `ANDROID_VR`/`IOS`.

### 11.3 ANDROID_VR — «сладкая точка» для медиа на JS-less устройстве
```http
POST https://www.youtube.com/youtubei/v1/player?prettyPrint=false
Content-Type: application/json
User-Agent: com.google.android.apps.youtube.vr.oculus/1.71.26 (Linux; U; Android 12; ...)

{"context":{"client":{"clientName":"ANDROID_VR","clientVersion":"1.71.26",
 "deviceModel":"Quest 3","androidSdkVersion":32,"hl":"en","gl":"US"}},"videoId":"VIDEO_ID"}
# android_vr: нет player PoToken, нет JS-сигнатуры, отдаёт пригодные URL. Версия катится — сверять ежемесячно.
```

### 11.4 Рекомендация для тонкого клиента (Nokia N9, Qt 4.7.4, без JS)
1. **Только HTTP** — один `QNetworkAccessManager`, `POST` JSON. Ключ не нужен; ставить
   `X-YouTube-Client-Name/Version` + соответствующий `User-Agent`.
2. **Метаданные:** `search`/`browse`/`next` с **WEB** (свежая версия) или **TVHTML5**. Парсить
   renderer'ы (§7); пагинация continuation-токенами.
3. **Стримы:** `player` с **IOS**, анонимно → `streamingData.hlsManifestUrl`; отдать в медиа-стек как
   HLS. Fallback — **ANDROID** прогрессив `formats[*].url` (itag 18/22). **Не** реализовывать
   `s`/`n`-дешифровку и BotGuard.
   - **🔬 Проверить на устройстве первым делом:** что IOS HLS реально играет и **не тротлится** (что
     ни манифест, ни сегменты не требуют `n`-трансформации). Это несущее допущение всего плана.
4. **Auth (опц., позже):** OAuth **TV device-code + QR** — идеально под `AccountManager` `device`/`qr`.
   Хранить только `refresh_token`; Bearer — на browse/subscribe/like, **никогда** на `player`.
5. **Анти-устаревание:** держать версии клиентов в одной структуре; если WEB-вызовы начинают падать —
   поднять до текущих значений из yt-dlp `_base.py`.

> По сути это **C++/Qt-порт проверенного WP-клиента Dmitry**, минус WebView-`nsig` (заменён
> предпочтением IOS HLS). Именно этим путём идёт MeeTube (§12).

---

## 12. Анализ локальных проектов (`/opt/projects/innertube-examples/`)

### 12.1 Dmitry's WP YouTube — **эталон legacy** (C#/UWP, Windows Phone, работает в 2026)
Полноценное приложение (home, search, channel, playlist, video, shorts, subscriptions, history,
comments, login) **целиком на InnerTube**. Мульти-клиентная стратегия. Все значения вшиты в
`YouTube/Config.cs`.

**Хардкод-константы (Config.cs):**
| Константа | Значение |
|---|---|
| `InnertubeApiKey` | `AIzaSyAO_FJ2SlqU8Q4STEHLGCilw_Y9_11qcW8` |
| `OAuthClientId` | `861556708454-d6dlm3lh05idd8npek18k6be8ba3oc68.apps.googleusercontent.com` |
| `OAuthClientSecret` | `SboVhoG9s0rNafixCSGGKXAT` |
| OAuth token URL | `https://oauth2.googleapis.com/token` |
| InnerTube base | `https://www.youtube.com/youtubei/v1` |
| `HomeWebClientVersion` | `2.20260430.08.00` (WEB) |
| `ShortsWebClientVersion` | `2.20260206.01.00` (WEB) |
| WEB (общий) | `2.20250101` |
| `MWEB` version | `2.20251222.01.00` |
| `TVHTML5` version | `7.20250209.19.00`, X-YouTube-Client-Name `85` |
| `ANDROID` version | `20.10.38`, X-YouTube-Client-Name `3`, `androidSdkVersion 30` |
| UA TVHTML5 | `Mozilla/5.0 (SMART-TV; Linux; Tizen 6.0)` |
| UA WEB | `Mozilla/5.0 (Windows NT 10.0; Win64; x64) … Chrome/124.0.0.0 Safari/537.36` |
| UA ANDROID | `com.google.android.youtube/20.10.38 (Linux; U; Android 11) gzip` |
| `ShortsVisitorId` (X-Goog-Visitor-Id) | `CgtjTS00dGRYTXhBOCif8OnOBjIoCgJQTBIiEh4SHAsMDg8QERITFBUWFxgZGhscHR4fICEiIyQlJicgSA%3D%3D` |

**Ключевые наблюдения:**
- **Мульти-клиент:** TVHTML5 (id 85) с Bearer для authed browse/subscriptions; ANDROID (id 3) для
  анонимного/Shorts-плеера; WEB (id 1) для поиска/Shorts-fallback; MWEB для мобильных Shorts.
  > ⚠ Нюанс: Dmitry использует `X-YouTube-Client-Name: 85` вместе с `clientName: "TVHTML5"`. Канонически
  > id 85 = `TVHTML5_SIMPLY_EMBEDDED_PLAYER`, а TVHTML5 = 7 (см. §5). Это особенность данного клиента —
  > отмечено как расхождение, не нормализуем.
- **Плеер без дешифровки:** берёт готовый URL прямо из `streamingData.formats[]/adaptiveFormats[]`,
  иначе `hlsManifestUrl`. **Нет** sig/n-дешифровки (поиск по коду — функций дешифровки нет).
- **Bearer-ловушка:** `/player` намеренно зовётся **без** OAuth первым (комментарий в коде:
  *«ANDROID player endpoint often rejects Bearer tokens with 400 INVALID_ARGUMENT»*) → порядок
  ANDROID-anon → WEB-anon → WEB+Bearer.
- **Age/racy:** в теле `player` шлёт `contentCheckOk:true, racyCheckOk:true`. Явного age-gate-обхода нет.
- **Парсинг:** маркеры рендереров `videoRenderer, gridVideoRenderer, compactVideoRenderer,
  playlistVideoRenderer, playlistPanelVideoRenderer, lockupViewModel, reelItemRenderer,
  shortsLockupViewModel, tileRenderer`; метаданные из `videoMetadataRenderer`/
  `videoPrimaryInfoRenderer`/`videoSecondaryInfoRenderer`; комментарии из `engagementPanels` →
  `commentEntityPayload`; continuation для бесконечного скролла.

**Сетевое происхождение:** в коде **нет** ссылки на репозиторий/автора; идентифицируют только имя
папки «Dmitry's WP YouTube» и OAuth-креды. Внешний origin (GitHub/4PDA/XDA/w10m) по доступным данным
**не найден — помечается как локальный источник** (выдумывать ссылку нельзя).

> **Боевая значимость:** Dmitry WP доказывает, что **в 2026 можно сделать рабочий YouTube-клиент на
> старой платформе без BotGuard и без дешифровки**, опираясь на IOS HLS / ANDROID progressive +
> мульти-клиентный fallback + TV-OAuth. Это прямой шаблон для MeeTube.

### 12.2 LuanRT/YouTube.js (youtubei.js) — самый полный клиент (TS)
- **Хосты:** PRODUCTION_1 `https://www.youtube.com/youtubei/`, PRODUCTION_2
  `https://youtubei.googleapis.com/youtubei/`, + STAGING/RELEASE sandbox.
- **Версии клиентов (Constants.ts, свежие на 2026-06):** WEB `2.20260623.01.00`, MWEB
  `2.20260205.04.01`, ANDROID `21.03.36` (SDK 36), IOS `20.11.6`, ANDROID_VR `1.65.10`, ANDROID_MUSIC
  `5.34.51`, TVHTML5 `7.20260311.12.00`, WEB_REMIX `1.20250219.01.00`, WEB_EMBEDDED `1.20260206.01.00`.
- **Карта id:** WEB=1, MWEB=2, ANDROID=3, IOS=5, TVHTML5=7, ANDROID_CREATOR=14, ANDROID_MUSIC=21,
  ANDROID_VR=28, WEB_EMBEDDED=56, WEB_CREATOR=62, WEB_REMIX=67, TVHTML5_SIMPLY=74,
  WEB_KIDS=76, TVHTML5_SIMPLY_EMBEDDED_PLAYER=85.
- **VisitorData:** генерится локально (`encodeVisitorData(randomId(11), ts)`), статичный fallback
  `6zpwvWUNAco`.
- **Дешифровка:** AST-извлечение (`JsAnalyzer`/`JsExtractor`) `nsigFunction` + `signatureTimestamp` из
  `base.js` (`player_es6.vflset`), `eval` через `Platform.shim`; кэш плеера по `player_id`.
- **PoToken:** `po_token` задаётся при создании сессии, добавляется как `pot=` к URL (не-SABR), либо в
  тело `player`.
- **Endpoints:** `/player /search /browse /next /reel /updated_metadata /notification/get_notification_menu
  /att/get /guide /get_transcript /live_chat/get_item_context_menu /navigation/resolve_url`.
- **CHANGELOG (breaking):** v17.0.0 (2026-03-16) — удалён `getTrending`, изменены `SearchFilters`;
  v16.0.0 (2025-10-12) — async JS-evaluator, AST-извлечение обязательно.

### 12.3 tombulled/innertube — чистая модель «endpoint + context» (Python)
- **Один хост:** `https://youtubei.googleapis.com/youtubei/v1/` (без per-service хостов; рефералы
  различают сервисы).
- **56 клиентов** в `config.py` (WEB, MWEB, ANDROID, IOS, TVHTML5, TVLITE, *_CREATOR, *_KIDS, *_MUSIC,
  ANDROID_VR, WEB_REMIX, WEB_KIDS, TVHTML5_SIMPLY, TVHTML5_SIMPLY_EMBEDDED_PLAYER, …); у 28 есть API-
  ключи (таблица в §3.5), у 28 — `None`.
- **7 core-endpoints:** `config, guide, player, browse, search, next, get_transcript` + `music/
  get_search_suggestions`, `music/get_queue`. Все POST, тело JSON.
- **Context:** `client.{clientName, clientVersion, gl, hl}`. **Заголовки:** `X-Goog-Api-Format-Version:
  1`, `X-YouTube-Client-Name` (=client_id), `X-YouTube-Client-Version`, `User-Agent`, `Referer`,
  `Accept-Language`. **Query:** `key`, `alt=json`.
- **VisitorData:** извлекается из `responseContext.visitorData` и кэшируется в `X-Goog-Visitor-Id`.
- **OAuth/device-code:** **не реализован** (только аноним; в README — как future work).

### 12.4 MeeTube — собственный движок (локальная база, C++/Qt 4.7.4)
Порт стратегии Dmitry WP на Qt. Engine `src/innertube/` (clientconfig/contextbuilder/innertubeclient/
innertube), парсеры `src/parsers/` (renderer/player/continuation). Импersonates WEB
`2.20260626.01.00`, IOS `20.49.6`, ANDROID `20.10.38`, TVHTML5 `7.20260114.12.00`; consent-cookie
`SOCS=CAISAiAD`; OAuth TV device-code; предпочитает IOS HLS, без sig/n-дешифровки и BotGuard. Подробная
инженерная база — [`docs/INNERTUBE_API.md`](docs/INNERTUBE_API.md) (486 строк, cross-verified против
yt-dlp/NewPipe/Invidious).

> **Расхождение локального и внешнего:** Dmitry WP шлёт `?key=`, tombulled тоже; yt-dlp/NewPipe ключ
> **не шлют**. Оба варианта рабочие — ключ опционален (§3.4). Версии клиентов у Dmitry (WEB
> `2.20260430.08.00`) и YouTube.js (WEB `2.20260623.01.00`) отличаются — это нормально (катятся
> понедельно); важен формат `2.YYYYMMDD.NN.NN`, не конкретные цифры.

---

## 13. Известные breaking changes и хрупкость API

> Состязательно проверенная (3 голоса) сводка хрупких фактов — см. блок «Проверка» в конце раздела.

**Таймлайн (по issue/changelog, даты — `FRAGILE`):**
- **2024-08 (≈)** — массовый rollout **PoToken/BotGuard** на web-клиенты: web/mweb начинают требовать
  GVS-токен, иначе 403 / «sign in to confirm you're not a bot». Сообщество переключается на
  ANDROID/IOS/TV-клиенты (yt-dlp, rustypipe вводят `*-botguard`).
- **2024–2025** — постепенное закручивание **ANDROID/IOS**: то требуют, то нет po_token; надёжность
  «no-token»-клиентов деградирует; yt-dlp делает дефолтом **ANDROID_VR** и **tv**.
- **2025-10-12** — YouTube.js v16.0.0: переход на **async JS-evaluator + AST-извлечение** (старый
  путь сломан).
- **2025-11-12** — yt-dlp #15012: для web-клиентов нужен **внешний JS-рантайм** (Deno/Node/Bun/
  QuickJS) для nsig — встроенного интерпретатора больше не хватает.
- **2026-03-16** — YouTube.js v17.0.0: удалён `getTrending` (YouTube убрал endpoint), изменены
  `SearchFilters`.
- **Непрерывно** — **SABR/UMP** миграция: web и часть клиентов переводятся на server-driven ABR
  (`streamingData.serverAbrStreamingUrl`, контейнер UMP). `ANDROID_VR > 1.65` может форситься на SABR;
  это **главный долгосрочный риск** для plain-URL/HLS пути.

**Оси хрупкости (что перепроверять перед продакшеном):**
1. **Версии клиентов** — формат `2.YYYYMMDD.NN.NN` (WEB); конкретные цифры катятся понедельно. Брать
   из yt-dlp `_base.py`.
2. **Политика po_token по клиентам** — самая текучая часть; единственный авторитет — живой
   `INNERTUBE_CLIENTS`/`*PoTokenPolicy` yt-dlp.
3. **`ANDROID_VR`/`IOS` «no-token»** — линчпин no-BotGuard стратегии; деградирует.
4. **Age-gate** — классический embed-обход (клиент 85) **снят ~янв 2026** (см. §6.5); теперь только
   `web_creator`/`tv` + PO token + cookies. Не закладывайтесь на анонимный age-gate.
5. **SABR rollout** — если ANDROID_VR/IOS форсят на SABR-only, plain-URL/HLS путь ломается.
6. **TV OAuth-креды** — могут ротироваться; YouTube.js скрейпит их из TV `base.js`.

**Итог состязательной проверки (3 голоса на хрупкий факт):**

<!-- VERIFICATION_SUMMARY -->
Отдельный пайплайн прогнал **26 самых хрупких фактов** (версии клиентов, PoToken, сигнатура, age-gate,
auth) через **состязательную проверку в 3 независимых голоса** (нужно ≥2/3 «опровергнуто», чтобы факт
снять). Итог: **23 подтверждено, 2 сняты, 1 уточнён.**

**🔴 Снято (3/3 опровергли):**
1. **Age-gate через `TVHTML5_SIMPLY_EMBEDDED_PLAYER` (id 85, v2.0) + `thirdParty.embedUrl`** — больше
   **не** обходит age-gate. YouTube ~янв 2026 потребовал sign-in на каждое видео для этого клиента;
   yt-dlp удалил его в **PR #15787 / commit `8eb7943`** (2026-01-31). Параметры исторически верны, приём
   мёртв. (Раздел §6.5 уже исправлен.) Источник: github.com/yt-dlp/yt-dlp commit 8eb7943.
2. **Синтаксис yt-dlp `po_token=CLIENT+TOKEN`** (например `web+…`, `android+…`) — устарел. Текущий
   формат **требует контекст**: `--extractor-args "youtube:po_token=CLIENT.CONTEXT+TOKEN"`, где
   `CONTEXT ∈ {gvs, player, subs}` (несколько — через запятую: `web.gvs+XXX,web.player+YYY`); голый
   `CLIENT+TOKEN` теперь трактуется только как GVS-токен (legacy, до PR #12090 / commit `6b91d23`).
   Привязка `visitor_data` (logged-out) / `data_sync_id` (logged-in) и требование совпадения с сессией
   минтинга — подтверждены. Источник: yt-dlp PO Token Guide wiki.

**🟡 Уточнено (1/3 опровергли, факт выжил с поправкой):**
- **Матрица PoToken — Subs-токен.** Subs-PO-token нужен **только клиенту `web`**, а **не** `web_safari`
  (в таблице yt-dlp у `web_safari` отмечен только GVS). Остальное в матрице §6.3 верно: GVS =
  web/web_safari/mweb/tv_simply/web_music/web_creator/android/ios; Player = android/ios; без токена =
  tv/android_vr/web_embedded; Premium освобождены от GVS; HLS-live не требует токена, кроме ios.
  Источник: yt-dlp PO Token Guide wiki.

**✅ Подтверждено (3/3, ключевое):** формат версий `2.YYYYMMDD.NN.NN`; текущие версии WEB/мобильных
клиентов; `signatureTimestamp` из `base.js`; `att/get` как вход в BotGuard-флоу; аттестация
BotGuard/DroidGuard/iOSGuard (не cross-usable); три контекста PoToken (GVS/Player/Subs) и
session/content-binding; `visitorData` = protobuf {id, timestamp}; аноним работает для публичных видео;
OAuth device-code работает только с TV-клиентом; cookie-auth — самый функциональный режим;
`data_sync_id` биндит токен к авторизованной сессии; YouTube.js не несёт встроенного JS-интерпретатора
для дешифровки (нужен внешний рантайм); BgUtils v3.2.0 (MIT) не «обходит» BotGuard, а исполняет его.

> Каждый снятый/уточнённый факт уже отражён в соответствующих разделах. Полный машинный лог вердиктов:
> `tasks/wpa6foyw8.output` (26 фактов × 3 голоса = 78 проверок, 0 сбоев).
<!-- /VERIFICATION_SUMMARY -->

---

## 14. Источники

### Локальные (приоритет «проверено в бою»)
- `/opt/projects/innertube-examples/Dmitry's WP YouTube/YouTube/Config.cs` (+ `VideoAPI.cs`) — рабочий
  WP-клиент; источник §11/§12.1. **Сетевой origin не найден** (локальный источник).
- `/opt/projects/innertube-examples/innertube/` — tombulled/innertube (Python).
- `/opt/projects/innertube-examples/YouTube.js/` — LuanRT/youtubei.js (TS).
- `/opt/projects/MeeTube/docs/INNERTUBE_API.md` — инженерная база MeeTube (cross-verified 2026-06-29).

### Библиотеки-реализации
- LuanRT/YouTube.js — https://github.com/LuanRT/YouTube.js ; доки https://ytjs.dev/
- yt-dlp — https://github.com/yt-dlp/yt-dlp (extractors `youtube/`); PO Token Guide:
  https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide
- TeamNewPipe/NewPipeExtractor — https://github.com/TeamNewPipe/NewPipeExtractor
- tombulled/innertube — https://github.com/tombulled/innertube
- pytube `innertube.py` (DeepWiki) — https://deepwiki.com/pytube/pytube/3.4-innertube-api ;
  JuanBindez/pytubefix — https://github.com/JuanBindez/pytubefix
- LuanRT/BgUtils — https://github.com/LuanRT/BgUtils ; Brainicism/bgutil-ytdlp-pot-provider
- Tyrrrz/YoutubeExplode — https://github.com/Tyrrrz/YoutubeExplode
- zerodytrash/YouTube-Internal-Clients — https://github.com/zerodytrash/YouTube-Internal-Clients
- haxzie/innerTube.js — https://github.com/haxzie/innerTube.js
- MohammadKobirShah/InnerTube-API ; davidzeng0/innertube ; sigma67/ytmusicapi ; kkdai/youtube ;
  ThetaDev/rustypipe ; Benjamin-Loison/YouTube-operational-API ; Hexer10/youtube_explode_dart

### Reverse-engineering / разборы
- Oleksii Holub (tyrrrz.me), «Reverse-Engineering YouTube (Revisited)» —
  https://tyrrrz.me/blog/reverse-engineering-youtube-revisited
- leptos-null gist (`YTApiaryDeviceCrypto` / `X-Goog-Device-Auth`) —
  https://gist.github.com/leptos-null/8792b9c50fddc00cf525ed5055a872dc
- nadimtuhin, ytranscript — https://nadimtuhin.com/blog/ytranscript-how-it-works
- gatecrasher777 (research по внутренностям InnerTube) — _репозитории/посты: требует точной ссылки._
- menmob/innertube-documentation wiki ; brutecat «Decoding Google»

### Проекты-потребители
- Invidious — https://github.com/iv-org/invidious (+ invidious-companion); Piped —
  https://github.com/TeamPiped/Piped ; FreeTube ; LibreTube ; SmartTube ; InnerTune ; Rehike ; Clipious

### История продукта
- Gizmodo, «How Project InnerTube Helped Pull YouTube Out of the Gutter».
- Fast Company, «To Take On HBO And Netflix, YouTube Had To Rewire Itself».

### Поиск вширь
- GitHub topics: `innertube`, `youtubei`, `youtube-client`, `newpipe-extractor`.
- Code search: `youtubei/v1`, `INNERTUBE_API_KEY`, `clientName`, `visitorData`, `signatureTimestamp`.
- Issue-треды yt-dlp / youtubei.js (breaking changes); Reddit r/youtubedl, r/invidious.

---

> _Документ собран многоагентным deep-research пайплайном (анализ локальных исходников + 6 веб-
> направлений + состязательная верификация хрупких фактов) и сшит с battle-tested локальной базой.
> Перед использованием хрупких (`FRAGILE`) значений в продакшене — перепроверьте по живому yt-dlp._
