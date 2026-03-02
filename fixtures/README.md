# fixture bundles

Runtime-contract fixture bundles are stored as:

- `fixtures/<name>/config.json`
- `fixtures/<name>/seed.json`
- `fixtures/<name>/events.jsonl`
- `fixtures/<name>/expected_metrics.json`
- `fixtures/<name>/manifest.json`

Update and verify using:

```bash
python3 tools/fixtures/update_fixture.py
python3 tools/fixtures/verify_fixture.py
```
