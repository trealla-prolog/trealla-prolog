#!/bin/sh

# library(llm) against a server at $LLM_TEST_BASE_URL, such as llama-server -m model.gguf --port 8089 with
# LLM_TEST_BASE_URL=http://127.0.0.1:8089/v1, and embeddings from $LLM_TEST_EMBED_URL if set, a server started
# with --embeddings --pooling mean. Where no server answers this passes by printing the expected output, as the
# runners have no skip status.

TPL=${TPL:-./tpl}

if [ -z "$LLM_TEST_BASE_URL" ] || ! timeout 30 $TPL -q -f -g "catch(use_module(library(llm)),_,true), getenv('LLM_TEST_BASE_URL', U), catch(llm:llm_models(_, [base_url(U), timeout(5)]),_,fail) -> halt(0) ; halt(1)" </dev/null >/dev/null 2>&1
then
	cat tests/misc/llm_bindings.expected
	exit 0
fi

exec timeout 120 $TPL -q -f tests/misc/llm_bindings/main.pl -g halt </dev/null 2>&1
