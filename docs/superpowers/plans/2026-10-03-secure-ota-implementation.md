# Secure OTA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the experimental STM32F103ZET6 updater with a signed, bounded, power-loss-recoverable Primary/Secondary OTA bootloader.

**Architecture:** The application always links at `0x08008000`. YModem stores a signed package in Secondary, the bootloader validates SHA-256 and Ed25519, then installs it through a journaled page swap using Scratch; an unconfirmed trial image is swapped back automatically.

**Tech Stack:** STM32F103 HAL/C11, USART1 DMA + IDLE + FIFO, YModem-1K, CRC16/CRC32, SHA-256, verification-only Ed25519 implementation, host-native C tests.

**Spec:** `docs/superpowers/specs/2026-10-03-secure-ota-design.md`

## Global Constraints

- Target is STM32F103ZET6 with 512 KiB Flash, 64 KiB SRAM and 2 KiB erase pages.
- Flash regions and addresses must exactly match the approved design.
- Application link and vector address is always `0x08008000`.
- No GUI or standalone package-building tool is included in this stage.
- No private signing key may enter the repository.
- Ed25519 primitives must come from a maintained, license-compatible implementation and must not be handwritten.
- Existing USART1 115200 DMA/IDLE/FIFO transport remains the physical OTA channel.
- OTA protocol bytes and debug logging may not interleave.
- All writes must be range checked and read back before metadata advances.
- Do not commit or push until the user explicitly requests it.

## Review Focus

- Arithmetic overflow in package offsets must return a range error before any Flash erase; covered by Task 3 parser tests.
- Reset between a Flash write and metadata commit must resume idempotently; covered by Task 6 phase-injection tests.
- A valid signature for another product or hardware revision must still be rejected; covered by Task 5 policy tests.
- UART FIFO overflow during transfer must abort explicitly rather than install a truncated image; covered by Task 7 transport tests.
- A valid MSP with an invalid/non-Thumb reset vector must never be jumped to; covered by Task 8 boot-vector tests.

---

### Task 1: Host-testable foundation and authoritative Flash layout

**Files:**
- Create: `OTA/ota_types.h`
- Create: `Platform/flash_layout.h`
- Create: `tests/CMakeLists.txt`
- Create: `tests/test_flash_layout.c`
- Modify: `MDK-ARM/ZET6_BT.uvprojx`

**Interfaces:**
- Produces: `ota_status_t`; region constants `OTA_BOOT_*`, `OTA_PRIMARY_*`, `OTA_SECONDARY_*`, `OTA_SCRATCH_*`, `OTA_METADATA_*`; checked helpers `ota_range_is_valid(uint32_t address, uint32_t length, uint32_t start, uint32_t size)` and `ota_u32_add_overflow(uint32_t a, uint32_t b, uint32_t *result)`.

- [ ] **Step 1: Add failing layout tests** asserting exact addresses, 2 KiB alignment, no overlap, complete 512 KiB coverage, and overflow rejection.
- [ ] **Step 2: Configure and run the native C test executable**; expect failure because layout interfaces do not exist.
- [ ] **Step 3: Implement types, constants and checked arithmetic** with compile-time assertions.
- [ ] **Step 4: Add new source/include groups to Keil without changing generated HAL files.**
- [ ] **Step 5: Run native tests**; expect all Task 1 tests to pass.

### Task 2: Bounded Flash driver

**Files:**
- Create: `Platform/ota_flash.h`
- Create: `Platform/ota_flash.c`
- Create: `tests/test_ota_flash.c`
- Create: `tests/fakes/fake_flash.c`
- Create: `tests/fakes/fake_flash.h`
- Modify: `MDK-ARM/ZET6_BT.uvprojx`

**Interfaces:**
- Consumes: Task 1 layout and `ota_status_t`.
- Produces: `ota_flash_erase_region(uint32_t address, uint32_t length)`, `ota_flash_program(uint32_t address, const uint8_t *data, uint32_t length)`, `ota_flash_read(uint32_t address, void *data, uint32_t length)`, and region-specific permission checks.

- [ ] **Step 1: Add fake-Flash tests** for alignment, zero length, odd length, overflow, cross-region writes, Bootloader protection, HAL failure and readback mismatch.
- [ ] **Step 2: Run tests**; expect missing driver failures.
- [ ] **Step 3: Implement erase/program/read APIs** using halfword programming, bounded regions and mandatory readback.
- [ ] **Step 4: Run tests**; expect every valid write to match and every unsafe request to fail before mutation.

### Task 3: Canonical package manifest parser

**Files:**
- Create: `OTA/ota_image.h`
- Create: `OTA/ota_image.c`
- Create: `tests/test_ota_image.c`
- Create: `tests/fixtures/manifest_vectors.h`
- Modify: `MDK-ARM/ZET6_BT.uvprojx`

**Interfaces:**
- Consumes: Task 1 checked arithmetic and slot limits.
- Produces: `ota_image_manifest_t`, `ota_image_parse(const uint8_t *bytes, size_t length, ota_image_manifest_t *out)`, `ota_image_validate_layout(const ota_image_manifest_t *manifest)`, and canonical signed-message extraction.

- [ ] **Step 1: Add parser tests** for valid headers, bad magic/version/header size, unknown flags, nonzero reserved bytes, truncated fields, oversized payload, wrapped offsets, wrong link address and signature outside package bounds.
- [ ] **Step 2: Run tests**; expect missing parser failures.
- [ ] **Step 3: Implement explicit little-endian decoding** without casting input bytes to a packed C structure.
- [ ] **Step 4: Run tests**; expect all malformed inputs to return stable error codes without out-of-bounds reads.

### Task 4: CRC32 and SHA-256 integrity

**Files:**
- Create: `Crypto/ota_crc32.h`
- Create: `Crypto/ota_crc32.c`
- Create: `Crypto/ota_sha256.h`
- Create: `Crypto/ota_sha256.c`
- Create: `tests/test_ota_hash.c`
- Modify: `MDK-ARM/ZET6_BT.uvprojx`

**Interfaces:**
- Produces: streaming `ota_crc32_*` and `ota_sha256_init/update/final` APIs with fixed-width contexts.

- [ ] **Step 1: Add known-answer tests** for empty input, `123456789`, multi-block SHA-256 and byte-at-a-time streaming.
- [ ] **Step 2: Run tests**; expect missing hash symbols.
- [ ] **Step 3: Implement CRC32 and SHA-256** with no dynamic allocation.
- [ ] **Step 4: Run tests**; expect exact published digest values.

### Task 5: Ed25519 adapter and complete image policy

**Files:**
- Create: `Crypto/ota_ed25519.h`
- Create: `Crypto/ota_ed25519.c`
- Vendor: `ThirdParty/ed25519/<selected-library-files>`
- Create: `ThirdParty/ed25519/LICENSE`
- Create: `OTA/ota_auth.h`
- Create: `OTA/ota_auth.c`
- Create: `Platform/ota_public_key.c`
- Create: `tests/test_ota_auth.c`
- Create: `tests/fixtures/ed25519_vectors.h`
- Modify: `MDK-ARM/ZET6_BT.uvprojx`

**Interfaces:**
- Consumes: Task 3 manifest and Task 4 SHA-256.
- Produces: `ota_ed25519_verify(...)` and `ota_auth_verify_package(const ota_image_reader_t *reader, const ota_device_identity_t *device, uint32_t confirmed_counter, ota_verified_image_t *out)`.

- [ ] **Step 1: Select and record a maintained verification implementation** with license and upstream revision; expose only the local adapter.
- [ ] **Step 2: Add RFC/official known-answer signature tests**, including changed message, changed signature and changed public key.
- [ ] **Step 3: Add package-policy tests** for wrong product, incompatible hardware, digest mismatch, unknown key, forged signature, lower counter and same-counter reinstall policy.
- [ ] **Step 4: Run tests**; expect missing adapter/policy failures.
- [ ] **Step 5: Integrate the dependency and implement streaming package verification.**
- [ ] **Step 6: Run tests**; expect all cryptographic and policy vectors to pass.

### Task 6: Redundant metadata journal and resumable swap

**Files:**
- Create: `OTA/ota_metadata.h`
- Create: `OTA/ota_metadata.c`
- Create: `OTA/ota_swap.h`
- Create: `OTA/ota_swap.c`
- Create: `tests/test_ota_metadata.c`
- Create: `tests/test_ota_swap.c`
- Modify: `MDK-ARM/ZET6_BT.uvprojx`

**Interfaces:**
- Consumes: Task 2 Flash driver and Task 4 CRC32.
- Produces: `ota_metadata_load`, `ota_metadata_append`, `ota_metadata_transition`, `ota_swap_begin`, `ota_swap_step`, `ota_swap_resume`, and states defined by the spec.

- [ ] **Step 1: Add metadata tests** for blank Flash, one corrupt journal, torn record, generation wrap policy, full-page compaction and commit-marker-last behavior.
- [ ] **Step 2: Add swap tests** using fake Flash and reset injection after every Scratch/Primary/Secondary write and every journal transition.
- [ ] **Step 3: Run tests**; expect missing journal/swap failures.
- [ ] **Step 4: Implement dual append-only journals** selecting the newest valid committed record.
- [ ] **Step 5: Implement one-page-at-a-time three-phase exchange** with readback and idempotent resume.
- [ ] **Step 6: Run tests**; expect every injected reset to converge to a complete forward swap or complete rollback without mixed pages.

### Task 7: Hardened YModem transport into Secondary

**Files:**
- Create: `Transport/ota_transport.h`
- Create: `Transport/ota_ymodem.h`
- Create: `Transport/ota_ymodem.c`
- Modify: `SYSTEM/MY_USART.c`
- Modify: `SYSTEM/MY_USART.h`
- Modify: `BSP/MY_Ymodem.c`
- Modify: `BSP/MY_Ymodem.h`
- Create: `tests/test_ota_ymodem.c`
- Modify: `MDK-ARM/ZET6_BT.uvprojx`

**Interfaces:**
- Consumes: Task 2 Flash driver, Task 3 early header validation and existing DMA/FIFO receive path.
- Produces: `ota_transport_t` and `ota_ymodem_receive(ota_transport_t *transport, ota_download_result_t *out)`.

- [ ] **Step 1: Add scripted transport tests** for valid 128/1024-byte blocks, duplicate blocks, sequence errors, CRC errors, timeout, CAN cancellation, premature EOT, oversized declaration and FIFO dropped-byte detection.
- [ ] **Step 2: Run tests**; expect missing transport implementation.
- [ ] **Step 3: Refactor YModem behind `ota_transport_t`** and remove direct unrestricted Flash writes.
- [ ] **Step 4: Preserve DMA circular + IDLE + HT/TC receive** while making IRQ-state restoration and FIFO overflow explicit.
- [ ] **Step 5: Suppress diagnostic `printf` while protocol mode owns USART1.**
- [ ] **Step 6: Run tests**; expect deterministic ACK/NAK/CAN behavior and no installation state on failed transfers.

### Task 8: Safe boot, trial confirmation and rollback manager

**Files:**
- Create: `OTA/ota_boot.h`
- Create: `OTA/ota_boot.c`
- Create: `OTA/ota_manager.h`
- Create: `OTA/ota_manager.c`
- Create: `Application/ota_client.h`
- Create: `Application/ota_client.c`
- Create: `tests/test_ota_boot.c`
- Create: `tests/test_ota_manager.c`
- Modify: `Core/Src/main.c`
- Modify: `MDK-ARM/ZET6_BT.uvprojx`

**Interfaces:**
- Consumes: Tasks 3, 5, 6 and 7.
- Produces: `ota_boot_validate_vectors`, `ota_boot_jump` (target-only), `ota_manager_run`, and application API `OTA_ConfirmBoot()`.

- [ ] **Step 1: Add vector tests** for aligned SRAM MSP, invalid MSP, out-of-image handler, missing Thumb bit, blank vectors and overflowed image end.
- [ ] **Step 2: Add state-machine tests** for normal boot, verified install, resumable swap, trial confirmation, watchdog/reset attempt exhaustion and resumable rollback.
- [ ] **Step 3: Run tests**; expect missing manager/boot failures.
- [ ] **Step 4: Implement safe peripheral/interrupt teardown and dynamic VTOR/MSP jump.**
- [ ] **Step 5: Implement the boot state machine and application confirmation contract.**
- [ ] **Step 6: Replace the existing flag-byte logic and hardcoded jump in `main.c`.**
- [ ] **Step 7: Run tests**; expect every state transition and invalid-vector case to pass.

### Task 9: Linker configuration and target integration

**Files:**
- Modify: `MDK-ARM/ZET6_BT/ZET6_BT.sct`
- Create: `MDK-ARM/Application/APP_PRIMARY.sct`
- Modify: `EWARM/stm32f103xe_flash.icf`
- Create: `EWARM/stm32f103xe_app_primary.icf`
- Modify: `Core/Src/system_stm32f1xx.c` only if required for the application target
- Modify: `README.md`

**Interfaces:**
- Consumes: authoritative Task 1 addresses.
- Produces: Bootloader link restricted to 32 KiB and application link restricted to Primary.

- [ ] **Step 1: Restrict the Bootloader scatter file** to `0x08000000 + 0x8000` so linker overflow fails the build.
- [ ] **Step 2: Add application linker templates** based at `0x08008000` with a 224 KiB maximum.
- [ ] **Step 3: Document application vector relocation and `OTA_ConfirmBoot()` integration.**
- [ ] **Step 4: Build host tests and the Keil target**; expect no undefined symbols, no region overflow and no warnings introduced by OTA code.

### Task 10: End-to-end verification and recovery evidence

**Files:**
- Create: `docs/ota_protocol.md`
- Create: `docs/flash_layout.md`
- Create: `docs/signing_and_release.md`
- Create: `docs/recovery_test.md`
- Create: `tests/test_ota_end_to_end.c`

**Interfaces:**
- Consumes: all previous tasks.
- Produces: reproducible verification procedures and a native end-to-end simulation.

- [ ] **Step 1: Add an end-to-end test** that downloads a fixed signed fixture, verifies, swaps, trial-boots and confirms it.
- [ ] **Step 2: Add an end-to-end rollback test** that injects failed trial boots and verifies byte-for-byte restoration of the old Primary image.
- [ ] **Step 3: Run the full native suite**; expect all tests to pass under normal and sanitizer-enabled builds where available.
- [ ] **Step 4: Build the target and record ROM/RAM use**; fail if Bootloader exceeds its 32 KiB region.
- [ ] **Step 5: Document serial logs and an on-board interruption matrix** for download, verify, each swap phase, trial boot and rollback.
- [ ] **Step 6: Inspect `git diff --check` and repository status**; ensure generated build artifacts and private keys are absent.

