# IEM-500 Signal Flow

```text
input
  |
  v
+-----------------------------+
| HPF                         |
| independent of moduleBypass|
+-------------+---------------+
              |
              v
       +--------------+
       | moduleBypass |
       +------+-------+
              |
        +-----+-----+
        |           |
       true        false
        |           |
        |     +-----v------+
        |     | EQ filters |
        |     +-----+------+
        |           |
        +-----+-----+
              |
              v
      nonlinear / magnetic
              |
              v
            output
```

## State rule

A bypass transition must not reset the HPF state. A production implementation should also preserve constant latency across bypass states if a latency-bearing oversampled path is introduced.

## Current implementation note

The repository currently provides a reference digital HPF/LPF implementation and an experimental magnetic model. A topology-complete WDF implementation remains a separate work item and must be validated against an independently solved analog network.
