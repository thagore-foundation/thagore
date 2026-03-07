WASM_OUT = wasm/pkg
DEPLOY_REPO = git@github.com:thagore-foundation/playground.git
DEPLOY_BRANCH = gh-pages

.PHONY: wasm serve deploy all

wasm:
	cd wasm && wasm-pack build --target web --out-dir pkg --release
	rm -f wasm/pkg/.gitignore

serve:
	python3 -m http.server 8080

deploy:
	git subtree push --prefix playground $(DEPLOY_REPO) $(DEPLOY_BRANCH)

all: wasm
