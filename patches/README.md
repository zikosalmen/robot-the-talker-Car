# Patches

## `esp-ml307-skip-hostname.patch`

Desk bring-up only. Full write-up: documentation repo **software-spec-v0.md §4.4**.

`managed_components/` is gitignored. After `idf.py` fetches `78/esp-ml307`, from this repo root:

```sh
git apply patches/esp-ml307-skip-hostname.patch
```

Revert when `api.tenclass.net` presents a cert whose SAN includes `api.tenclass.net`.
