# CBOR Format Validation

Validates the CBOR diagnostic samples from the Flex 2.0 spec against their CDDL schemas.

## Prerequisites

```bash
gem install cbor-diag cddl
```

## Usage

```bash
./validate.sh          # run all validations
./validate.sh -v       # verbose — show byte sizes
```

## Structure

```
cbor-validation/
  prelude.cddl              Shared type definitions
  storage.cddl              Storage record schema
  lora-logger-relay.cddl    Logger->Relay schema
  lora-relay-portal.cddl    Relay->Portal schema
  samples/
    storage-simple.diag     9 temp probes + battery
    storage-splitters.diag  Chained splitters example
    storage-chunked-0.diag  Chunk 0 of 2
    storage-chunked-1.diag  Chunk 1 of 2 (no battery)
    storage-multi-chunk-0.diag  Multi-chunk 0: splitters, humidity, dual-temp
    storage-multi-chunk-1.diag  Multi-chunk 1: splitters, humidity, dual-temp
    storage-pm-sensor.diag      Particulate-matter sensor (SEN5x), 6 readings
    storage-air-quality.diag    Air-quality sensor (BME680), 6 readings
    storage-combined-0.diag     Combined chunk 0: ambient + PM sensor
    storage-combined-1.diag     Combined chunk 1: air-quality sensor
    lora-logger-relay.diag  Logger->Relay sample
    lora-relay-portal.diag  Relay->Portal sample
  validate.sh               Validation runner
```

## Notes

- `diag2cbor.rb` encodes non-f16-representable values (e.g. 12.78) as float32/64.
  The firmware uses float16 on wire, so actual encoded sizes will be smaller than
  what the validator reports.
- CDDL `.include` directives are resolved by the validate script at runtime.
