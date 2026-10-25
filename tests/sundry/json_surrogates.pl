% library(json) decodes \u surrogate pairs as one character and rejects lone surrogates #1181
:- use_module(library(json)).
:- initialization(main).

codes([], []).
codes([C|Cs], [N|Ns]) :- char_code(C,N), codes(Cs,Ns).
parsed(Wire, Codes) :-
    phrase(json_chars(string(Chars)), Wire), codes(Chars, Actual), Actual == Codes.
rejected(Wire) :- \+ phrase(json_chars(_), Wire).
scalar_round_trip(Code) :-
    char_code(Char,Code),
    once(phrase(json_chars(string([Char])), Wire)), parsed(Wire,[Code]).

test(ascii, parsed("\"ab\"", [97,98])).
test(bmp_escape, parsed("\"\\u00e5\"", [229])).
test(raw_supplementary, parsed("\"😀\"", [128512])).
test(surrogate_pair, parsed("\"\\ud83d\\ude00\"", [128512])).
test(uppercase_pair, parsed("\"\\uD83D\\uDE00\"", [128512])).
test(first_supplementary, parsed("\"\\ud800\\udc00\"", [65536])).
test(last_scalar, parsed("\"\\udbff\\udfff\"", [1114111])).
test(before_surrogates, parsed("\"\\ud7ff\"", [55295])).
test(after_surrogates, parsed("\"\\ue000\"", [57344])).
test(noncharacter_scalar, parsed("\"\\uffff\"", [65535])).
test(nul_escape, parsed("\"\\u0000\"", [0])).
test(adjacent_pairs, parsed("\"\\ud83d\\ude00\\ud83d\\udcbb\"", [128512,128187])).
test(mixed_raw_escaped, parsed("\"aé\\ud83d\\ude00\\n\"", [97,233,128512,10])).
test(literal_backslashes, parsed("\"\\\\ud83d\\\\ude00\"", [92,117,100,56,51,100,92,117,100,101,48,48])).
test(quote_backslash, parsed("\"\\\"\\\\\"", [34,92])).
test(empty, parsed("\"\"", [])).
test(high_alone, rejected("\"\\ud800\"")).
test(low_alone, rejected("\"\\udc00\"")).
test(high_then_bmp, rejected("\"\\ud800\\u0061\"")).
test(low_then_high, rejected("\"\\udc00\\ud800\"")).
test(two_highs, rejected("\"\\ud800\\udbff\"")).
test(two_lows, rejected("\"\\udc00\\udfff\"")).
test(truncated_pair, rejected("\"\\ud800\\udc0\"")).
test(separated_pair, rejected("\"\\ud800x\\udc00\"")).
test(nonhex_escape, rejected("\"\\uZZZZ\"")).
test(bound_pair_match, phrase(json_chars(string(['😀'])), "\"\\ud83d\\ude00\"")).
test(bound_wrong_scalar, \+ phrase(json_chars(string(['😀'])), "\"\\uf600\"")).
test(no_truncated_generation,
    \+ phrase(json_chars(string(['😀'])), ['"','\\',u,_,_,_,_,'"'])).
test(generate_ascii, scalar_round_trip(97)).
test(generate_bmp, scalar_round_trip(229)).
test(generate_nul, scalar_round_trip(0)).
test(generate_first_supplementary, scalar_round_trip(65536)).
test(generate_emoji, scalar_round_trip(128512)).
test(generate_last_scalar, scalar_round_trip(1114111)).
test(object_key_value, (
    phrase(json_chars(pairs([string(Key)-string(Value)])),
           "{\"\\ud83d\\ude00\":\"\\ud83d\\udcbb\"}"),
    codes(Key,[128512]),codes(Value,[128187]))).
test(array, (
    phrase(json_chars(list([string(Chars),number(7),boolean(true)])),
           "[\"\\ud83d\\ude00\",7,true]"),codes(Chars,[128512]))).
test(prolog_reader, (
    phrase(json_chars(string(Chars)), "\"\\\"\\ud83d\\ude00\\\"==['\\ud83d\\ude00']\""),
    atom_chars(Goal,Chars),read_term_from_atom(Goal,Term,[]),call(Term))).

run([], 0, 0).
run([Label-Goal|Cases], Passed, Failed) :-
    ( catch(call(Goal), Error, (writeq(error(Label,Error)),nl,fail)) ->
        Result=pass, P=1, F=0
    ; Result=fail, P=0, F=1 ),
    writeq(Result-Label),nl,
    run(Cases,Ps,Fs),Passed is Ps+P,Failed is Fs+F.
main :-
    findall(Label-Goal,test(Label,Goal),Cases),run(Cases,Passed,Failed),
    format('JSON Unicode: ~d passed, ~d failed~n',[Passed,Failed]),
    (Failed=:=0 -> halt ; halt(1)).
