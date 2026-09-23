# Message encoding on the snoop buses (MSI / MESI / MOESI split‑bus family)

Reference for anyone reading `Message` fields in traces, the Debugger output, or the Logger.
Validated against the protocol decoders (`MSIProtocol::readEvent`, `LLCMSIProtocol::readEvent`,
`MESIProtocol::readEvent`) and against dprint traces of full EEMBC/SPLASH runs (2026‑09‑22).

## The fields a decoder reads
| field | meaning |
|---|---|
| `source` | where the message entered the controller: `LOWER_INTERCONNECT` (from the cores' side), `UPPER_INTERCONNECT` (from the bus/memory side), `SELF` (a replacement the controller generated for itself) |
| `data` | `NULL` ⇒ a request/command; non‑NULL ⇒ a data transfer |
| `complementary_value` (`cv`) | **on a request:** its type — `0` GetS, `1` GetM, `2` PutM, `10` INV (CPU→L1: `0` Load, `1` Store). **On data:** *inherited* from whatever message the data was copied from — it is provenance, **not** a kind (see below) |
| `owner` | on requests and supplies: the **requester** core; on an eviction write‑back: the **evicting L1**; on an invalidation: the **LLC**; on a fill: the requester |
| `to` | destination interface ids (a bus response is delivered only to these; a broadcast request goes to everyone) |
| `msg_id` | a demand request keeps one id through GetS/GetM → fill → response; an eviction write‑back has its **own** id (the L1 replacement's); a write‑back forced by a back‑invalidation carries the **LLC eviction's** id |
| `kind` | (phase‑3 addition) the producer's classification, see the last section |

The bus `MessageType` given to `pushMessage` (REQUEST / DATA_RESPONSE / SERVICE_REQUEST) selects the channel (request bus / response bus / TripleBus service channel); it is not stored in the message.

## Requests (`data == NULL`)
| producer | `cv` | `owner` | `to` | `msg_id` | decoded as |
|---|---|---|---|---|---|
| CPU → L1 | 0 Load / 1 Store | — | — | new id | L1: `Load` / `Store` |
| L1 `GetS` / `GetM` action | 0 / 1 | requester L1 | [LLC] | the CPU request's id | LLC: `GetS`/`GetM`; other L1s: `Other_GetS/GetM`; self: `Own_GetS/GetM` (owner == m_id) |
| L1 `PutM` action (dirty eviction) | 2 | evicting L1 | [LLC] | new id (the L1 replacement SELF message) | LLC: `PutM_fromOwner` if owner == line owner, else `PutM_fromNonOwner`; L1s: `Own_PutM` / `Other_PutM` |
| LLC `IssueInv` (back‑invalidation) | 10 | LLC id | [LLC], broadcast on the **service** channel | the LLC replacement's id | L1s: `Invalidation`; LLC: `Own_Invalidation` |
| LLC `GetData` → memory | `(uint16_t)ActionId::GetData` = 1 | requester core | [memory] | request's id | memory: read |
| L1 / LLC replacement (SELF; never on a bus) | 0 | own id | — | new id (`IdGenerator`) | `Replacement` |

## Data (`data != NULL`) — `cv` is whatever the copied message had
| producer | copied from | `cv` observed | `owner` → `to` | decoded at the LLC / L1 |
|---|---|---|---|---|
| L1 eviction write‑back: `MI_a + Own_PutM → Data2Req` | the Own_PutM | 2 | evicting L1 → [LLC] | LLC `Data_fromLowerInterface` (count equals `PutM_fromOwner`) |
| write‑back forced by a back‑invalidation: `M/E/MI_a + Invalidation → Data2Req` | the INV | 10 | LLC id → [LLC] | LLC `Data_fromLowerInterface` |
| supply, immediate: `M/E + Other_GetS → Data2Both`, `M/E + Other_GetM → Data2Req` | the GetS / GetM | 0 / 1 | requester → [requester] (+ [LLC] for `Data2Both`) | requester `OwnData`; LLC `Data_fromLowerInterface` (S_d → S) |
| supply, deferred (parked request answered when own data arrives): `IM_dS/SM_dS/IES_d/IES_dI/IS_dI + OwnData(_Execlusive)` | the OwnData just received | 0 / 1 / 2 | parked requester(s); + [LLC] iff the parked head is a GetS | requester `OwnData`; LLC `Data_fromLowerInterface` |
| LLC response: `SendData` / `SendExeclusiveData` (HIT_Action / REMOVE_PENDING) | the request | 0 / 1; **2 = exclusive grant** (`LLCMESIProtocol` sets cv=2; the MESI L1 decodes data‑with‑cv‑2 as `OwnData_Execlusive`) | [requester] | L1 `OwnData` / `OwnData_Execlusive` |
| fill from MainMemory / MCsim / perfect‑LLC loopback | constructed | 0 | requester → [LLC] | LLC `Data_fromUpperInterface` |
| fill by **rollback** of a queued write‑back (End2End) | the GetData request | 1 | requester → [LLC] | LLC `Data_fromUpperInterface` |
| LLC write‑back to memory (`WriteBack` action) | constructed | 0 | `m_owner_of_latest_data` or LLC id → [memory] | memory: write |

**Three things the same value means.** `cv = 2` is a PutM on a request, an eviction write‑back at the LLC, and an exclusive grant at an L1. `owner` is the requester for requests/supplies/fills, the evicting L1 for write‑backs, the LLC for invalidations. `msg_id` joins a demand request across its life, but an invalidation‑forced write‑back is attributed to the LLC eviction, not to any core request.

## Consequence: kinds are assigned by the producer (`Message::kind`)
Because the fields above cannot be read back into a kind, the phase‑3 trace tags each message where it is created:

| kind | set by |
|---|---|
| `DEMAND` | CPU (trace sample) |
| `GETS`, `GETM`, `PUTM` | L1 `GetS`/`GetM`/`PutM` actions |
| `INV` | LLC `IssueInv` |
| `MEM_READ` | LLC `GetData` |
| `EVICT` | `checkReplacements` (SELF message, L1 or LLC) |
| `WB_DATA` | L1 `Data2Req/Data2Both` triggered by `Own_PutM` |
| `WB_INV` | L1 `Data2Req` triggered by `Invalidation`; also the data an L1 returns to the LLC when its own data arrives after it was invalidated while waiting (`IS_dI`/`IM_dI` + `OwnData`: nobody parked, the LLC waits in `MN_d`) — retagged in `CacheController::performWriteBack`, keeps the request's id and owner |
| `SUPPLY` | L1 `Data2Req/Data2Both` triggered by `Other_GetS`/`Other_GetM` |
| `SUPPLY_DEFERRED` | L1 `Data2Req/Data2Both` triggered by `OwnData`/`OwnData_Execlusive` with a parked request; the message takes the **parked** request's id and owner (and a copy goes to the LLC if the parked request was a GetS) |
| `RESP` | LLC `SendData` / `SendExeclusiveData` |
| `FILL`, `FILL_ROLLBACK` | MainMemory / MCsim / perfect loopback; End2End rollback |
| `MEM_WRITE` | LLC `WriteBack` |
| `0` (unknown) | anything else (directory protocols are not tagged yet) |

`Message::copy` propagates `kind`; the decoders never read it.
