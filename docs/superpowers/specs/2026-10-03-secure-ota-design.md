# STM32F103ZET6 Production OTA Design

## 1. Scope

This design replaces the current experimental A/B OTA implementation with a
power-loss-safe, signed, recoverable boot process for STM32F103ZET6.

Included in the first implementation stage:

- fixed and documented internal Flash layout;
- signed image container parsing;
- SHA-256 image integrity verification;
- Ed25519 authenticity verification with an embedded public key;
- anti-rollback version checks;
- YModem transport over USART1;
- Primary/Secondary image storage with power-loss-safe page swapping;
- trial boot, application confirmation, watchdog failure detection and rollback;
- redundant journaled metadata;
- strict image, address and vector-table validation;
- deterministic status codes and diagnostic logs;
- host-generated cryptographic test vectors for firmware verification tests.

Explicitly excluded from this stage:

- GUI packaging software;
- a standalone command-line OTA packaging utility;
- private signing keys in this repository;
- encryption of firmware contents;
- changing the physical UART/Bluetooth transport.

The package format is nevertheless frozen now so a packaging tool can be added
later without changing the bootloader protocol.

## 2. Hardware assumptions

- MCU: STM32F103ZET6.
- Internal Flash: 512 KiB, `0x08000000` through `0x0807FFFF`.
- SRAM: 64 KiB, `0x20000000` through `0x2000FFFF`.
- Flash erase page: 2 KiB.
- OTA transport: USART1, 115200 baud, DMA circular receive with IDLE, half and
  full transfer events feeding a software FIFO.
- The application is linked to one fixed execution address.

## 3. Chosen architecture

The system uses a fixed Primary execution slot and a Secondary download slot.
Both image payloads are linked for the Primary address. A verified update is
installed with a resumable page swap. This avoids maintaining separate A-slot
and B-slot application builds.

Alternative designs were rejected:

- Direct boot from two addresses requires two link variants or position
  independent firmware and creates a high risk of deploying the wrong image.
- Overwriting the active image directly cannot guarantee recovery after reset or
  power loss.
- External Flash is not required by the current hardware.

## 4. Flash layout

| Region | Start | End | Size | Purpose |
| --- | --- | --- | ---: | --- |
| Bootloader | `0x08000000` | `0x08007FFF` | 32 KiB | Boot, OTA, crypto, recovery |
| Primary | `0x08008000` | `0x0803FFFF` | 224 KiB | Executing application |
| Secondary | `0x08040000` | `0x08077FFF` | 224 KiB | Downloaded image / old image |
| Scratch | `0x08078000` | `0x0807BFFF` | 16 KiB | Atomic page exchange workspace |
| Metadata | `0x0807C000` | `0x0807FFFF` | 16 KiB | Redundant append-only journals |

All boundaries are compile-time asserted to be page aligned, non-overlapping and
inside the actual device Flash. The application linker base is
`0x08008000`; its maximum binary payload is less than the Primary slot size after
accounting for any slot trailer required by the swap algorithm.

## 5. Signed image container

The OTA input is an `.otapkg` byte stream rather than an unsigned raw `.bin`.
The transport treats it as opaque bytes.

### 5.1 Container layout

```text
offset 0                  signed fixed-size manifest
offset manifest_size      application payload
offset ...                64-byte Ed25519 signature
```

The manifest uses explicitly sized little-endian integers and contains no C
structure padding. Its initial version contains:

- magic `OTAP`;
- container format version;
- manifest size;
- product identifier;
- hardware compatibility revision range;
- semantic version fields;
- monotonic security counter;
- payload size;
- application link address (`0x08008000`);
- vector-table offset;
- build timestamp;
- source revision identifier;
- SHA-256 digest of the payload;
- signature algorithm and key identifier;
- manifest CRC32 for early corruption detection;
- reserved bytes that must be zero.

The Ed25519 signature covers a domain-separation string, the canonical manifest
with its signature-related mutable fields normalized, and the payload SHA-256
digest. The bootloader accepts only the exact supported format version and key
identifier. Unknown flags and nonzero reserved fields are rejected.

CRC32 detects accidental transport/storage errors. SHA-256 and Ed25519 establish
integrity and authenticity. CRC is never treated as a security mechanism.

## 6. Key and version policy

- The Ed25519 private key never exists on the target or in this repository.
- The bootloader contains one production public key and may optionally contain a
  separately identified development public key in development builds only.
- Production builds reject the development key at compile time.
- A monotonic security counter prevents installation of older vulnerable images.
- Semantic version is informational; the security counter controls rollback
  protection.
- A physical recovery policy can permit reinstalling the same counter but never a
  lower counter unless a separately built factory-recovery bootloader is used.

## 7. Metadata journal

The existing byte flags are replaced with two independent append-only journals.
Each record contains:

- record magic and format version;
- generation number;
- OTA state;
- active/trial image information;
- package size and payload size;
- payload digest and security counter;
- swap page index and swap phase;
- trial boot attempt count;
- last reset and failure reason;
- CRC32;
- commit marker written last.

A record is valid only when its commit marker, bounds and CRC are valid. At boot,
the newest valid generation from either journal is selected. Journal compaction
is performed in the other erase page before the old page is erased, so one valid
copy always survives power loss.

States are:

```text
IDLE -> DOWNLOADING -> DOWNLOADED -> VERIFIED -> SWAPPING
SWAPPING -> TRIAL -> CONFIRMED -> IDLE
TRIAL -> ROLLBACK -> IDLE
```

## 8. Download and verification flow

1. The user explicitly enters OTA mode or the application requests it through a
   retained boot request.
2. Secondary is erased only after container header validation establishes that
   the package can fit.
3. YModem packets are checked by their protocol CRC16 and written sequentially.
4. Every programmed Flash halfword is checked for HAL errors and read back.
5. Download progress is journaled at coarse safe checkpoints.
6. On transfer completion, the exact declared package length must match.
7. The manifest CRC, product/hardware IDs, link address, sizes and vectors are
   checked.
8. SHA-256 is calculated over the stored payload.
9. Ed25519 verifies the signed manifest and digest using the embedded public key.
10. The security counter is checked against confirmed metadata.
11. Only then is the image marked `VERIFIED` and installation requested.

Logs must not share bytes with an active YModem exchange. Protocol responses use
the transport writer directly; optional diagnostics are suppressed or routed to
a separate debug interface while receiving.

## 9. Power-loss-safe installation

The application always executes from Primary. Installation exchanges one 2 KiB
page at a time:

1. copy Primary page to Scratch and verify it;
2. journal the completed phase;
3. copy Secondary page to Primary and verify it;
4. journal the completed phase;
5. copy Scratch page to Secondary and verify it;
6. journal page completion and advance the page index.

After any reset, the phase and page index determine the single idempotent action
needed to resume. Metadata is advanced only after the corresponding readback
verification succeeds. Flash write APIs enforce region ownership and cannot
erase or program the Bootloader from an OTA path.

## 10. Trial boot and rollback

After installation, the new Primary image is marked `TRIAL`. The bootloader:

- validates the Primary manifest/digest before jumping;
- increments the trial attempt count;
- starts an independent watchdog or requires the application to do so early;
- jumps only when the vector table is valid.

The application performs its essential startup self-tests and calls
`OTA_ConfirmBoot()` within a defined confirmation window. Confirmation commits
the new security counter and changes the state to `CONFIRMED`.

If the application resets repeatedly, faults, or fails to confirm, the
bootloader performs the same page exchange in reverse. Secondary then restores
the previously confirmed image. Rollback itself is resumable after power loss.

## 11. Safe application jump

Before jumping, the bootloader verifies:

- initial MSP is aligned and inside SRAM;
- reset vector has the Thumb bit set and points inside the valid Primary image;
- vector table and payload sizes are in range;
- image authentication and state permit boot.

It then disables interrupts, SysTick and DMA requests, clears pending NVIC
interrupts, deinitializes owned peripherals, sets `SCB->VTOR` to `0x08008000`,
loads MSP and branches to the reset handler. No jump address is hardcoded outside
the shared Flash layout definition.

## 12. Module boundaries

```text
OTA/ota_manager       top-level boot state machine
OTA/ota_image         manifest parsing and image validation
OTA/ota_metadata      redundant journal persistence
OTA/ota_swap          resumable page exchange
OTA/ota_boot          vector validation and application jump
Crypto/sha256         streaming payload digest
Crypto/ed25519_verify verification-only cryptographic adapter
Transport/ota_transport transport-neutral byte interface
Transport/ota_ymodem  YModem protocol adapter
Platform/flash_layout authoritative memory map
Platform/flash_driver bounded erase/program/readback operations
Platform/uart_dma     DMA/IDLE/FIFO receive implementation
Application/ota_client application confirmation contract
```

The Ed25519 implementation must come from a maintained, license-compatible,
verification-only implementation. Cryptographic primitives will not be written
from scratch.

## 13. Diagnostics

Status reporting uses stable machine-readable error codes plus concise text.
Messages identify the stage and cause, for example:

```text
[OTA][RX][INFO] package header accepted, payload=87324
[OTA][AUTH][ERROR] signature verification failed
[OTA][SWAP][INFO] page=17/109 phase=secondary-to-primary
[OTA][BOOT][WARN] trial attempt=2/3
[OTA][ROLLBACK][INFO] previous confirmed image restored
```

The first implementation does not include a graphical progress or color system.
If a host UI is added later, INFO, WARN, ERROR and SUCCESS codes provide the
semantic categories for consistent coloring.

## 14. Verification requirements

Implementation is not complete until tests cover:

- valid signed update and confirmation;
- malformed and truncated manifests;
- payload length overflow and slot overflow;
- changed payload after signing;
- forged signature and unknown key ID;
- wrong product/hardware identifiers;
- lower security counter;
- YModem timeout, duplicate block and cancellation;
- reset during download;
- reset at every swap phase for representative pages;
- reset before and after metadata commit;
- invalid Primary vectors;
- trial watchdog reset and automatic rollback;
- rollback interrupted by reset;
- Flash erase/program/readback failure injection;
- compile-time and runtime Flash layout validation.

Existing successful USART DMA/FIFO behavior is retained as a transport
foundation, but its overflow and interrupt-state handling is hardened during
implementation.

