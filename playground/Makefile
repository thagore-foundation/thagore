WASM_OUT = wasm/pkg

.PHONY: wasm serve deploy all

wasm:
	cd wasm && wasm-pack build --target web --out-dir pkg --release

serve:
	python3 -m http.server 8080

deploy:
	git subtree push --prefix playground origin gh-pages

all: wasm
