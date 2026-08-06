#!/usr/bin/env python3
"""
Eksperyment 4.3, Etap C — formatowanie etykiet z Etapu B (etap_b_labels.json)
jako pary (prompt, completion) w formacie fine-tuningowym OpenAI
(messages: system/user/assistant), w SCHEMACIE IDENTYCZNYM z tym, ktorego
uzywa DecodingAccuracyRunner (system prompt, user prompt z recentFrames +
trigger frame, odpowiedz JSON z lista "signals") - zeby douczony model dalo
sie podstawic w istniejacy pipeline bez zadnych zmian w kodzie C++.

WAZNA DECYZJA PROJEKTOWA: completion (target/"poprawna odpowiedz" do nauki)
buduje sie z etykiet KLASYFIKATORA (naszego "nauczyciela"), NIE z ground
truth bezposrednio - to jest cala idea destylacji z Etapu B. Skala/offset dla
sygnalow SKALARNYCH (nie-flagowych) pochodza z ground truth generatora
(klasyfikator Kierunku B nie probuje ich zgadywac - to poza jego zakresem,
patrz DecodingAccuracyRunner.cpp). Kazdy przyklad ma dolaczona flage
`labelMatchesGroundTruth`, zeby jawnie widziec, ile "zaszumionych" (blednie
wyetykietowanych przez klasyfikator) przykladow trafia do zbioru - zgodnie
z ostrzezeniem z propozycji Etapu B.
"""
import argparse
import json

BASE_SYSTEM_PROMPT = (
    "You are a CAN bus reverse-engineering assistant. A CAN message (fixed ID) "
    "typically packs SEVERAL distinct signals into its payload bytes (e.g. one "
    "message might contain both engine RPM as a 2-byte value AND a temperature "
    "as a 1-byte value). Analyze the trigger frame and the recent frames for this "
    "ID (which show how values change over time) and identify ALL signals you can "
    "confidently distinguish, then propose a decoding rule for each.\n\n"
    "Respond with ONLY a JSON object (no markdown fences, no prose outside the "
    "JSON) with this exact shape:\n"
    "{\n"
    "  \"interpretation\": \"short description of the message\",\n"
    "  \"signals\": [\n"
    "    {\n"
    "      \"name\": \"short_signal_name\",\n"
    "      \"byteIdx\": <int, 0-based starting byte offset>,\n"
    "      \"byteLen\": <1 or 2, number of bytes the signal spans>,\n"
    "      \"littleEndian\": <bool, true if multi-byte signal is little-endian>,\n"
    "      \"isSigned\": <bool, true if the value can be negative (two's complement)>,\n"
    "      \"bitMask\": <hex string like \"0x01\" if this signal is a SINGLE BIT "
    "within byteIdx (discrete on/off state), or null if it uses the full byte(s)>,\n"
    "      \"scale\": <float, physical_value = raw * scale + offset>,\n"
    "      \"offset\": <float>\n"
    "    }\n"
    "  ],\n"
    "  \"confidence\": <float 0.0-1.0>\n"
    "}\n\n"
    "Discrete on/off states (lights, doors, indicators) are single bits within one "
    "byte — use bitMask to isolate the bit, byteLen=1, scale=1, offset=0. Continuous "
    "measurements (RPM, temperature, angle, speed) use byteLen=1 or 2 depending on "
    "the value range you observe, bitMask=null, and scale/offset chosen so the "
    "decoded physical value matches plausible real-world magnitudes for that quantity."
)


def format_frame_list(frames, can_id, dlc, max_frames=None):
    lines = []
    for i, data in enumerate(frames):
        if max_frames is not None and i >= max_frames:
            break
        hexstr = "".join(f"{b:02X}" for b in data[:dlc])
        lines.append(f"  ID=0x{can_id:03x} DLC={dlc} data=[{hexstr}]")
    return "\n".join(lines)


def build_completion_from_classifier(cfg, byte_labels):
    """Buduje docelowy JSON (completion) z etykiet klasyfikatora (Etap B) +
    ground-truth scale/offset dla bajtow NIE zaklasyfikowanych jako flagi
    (klasyfikator nie zgaduje skali - to poza jego zakresem, patrz modul)."""
    dlc = cfg["dlc"]
    true_by_byte = {}
    for s in cfg["signals"]:
        if s["kind"] in ("scalar",):
            for off in range(s["byte_len"]):
                true_by_byte.setdefault(s["byte_idx"], s)

    signals_out = []
    covered_bytes = set()
    for byte_idx_str, lbl in byte_labels.items():
        byte_idx = int(byte_idx_str)
        if byte_idx in covered_bytes:
            continue
        if lbl["classifier_says_bit_flags"]:
            mask = lbl["classifier_mask"]
            for b in range(8):
                if mask & (1 << b):
                    signals_out.append({
                        "name": f"byte{byte_idx}_bit{b}",
                        "byteIdx": byte_idx, "byteLen": 1, "littleEndian": True,
                        "isSigned": False, "bitMask": f"0x{1 << b:02x}",
                        "scale": 1.0, "offset": 0.0,
                    })
            covered_bytes.add(byte_idx)
        else:
            gt = true_by_byte.get(byte_idx)
            if gt is None:
                continue  # bajt nieuzywany/padding - pomijamy w completion
            signals_out.append({
                "name": gt["name"], "byteIdx": gt["byte_idx"], "byteLen": gt["byte_len"],
                "littleEndian": gt["little_endian"], "isSigned": gt["is_signed"],
                "bitMask": None, "scale": gt["scale"], "offset": gt["offset"],
            })
            covered_bytes.update(range(gt["byte_idx"], gt["byte_idx"] + gt["byte_len"]))

    return {
        "interpretation": f"Auto-labeled by classical classifier (Kierunek B), CAN ID 0x{cfg['can_id']:03x}",
        "signals": signals_out,
        "confidence": 0.7,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--corpus", default="../esp_experiment_4_4_qdrant/corpus_diverse.json")
    ap.add_argument("--labels", default="etap_b_labels.json")
    ap.add_argument("--out", default="etap_c_finetune_data.jsonl")
    ap.add_argument("--window", type=int, default=30)
    args = ap.parse_args()

    corpus = json.load(open(args.corpus))
    labels = json.load(open(args.labels))["labels"]

    n_examples = 0
    n_clean = 0
    with open(args.out, "w") as out_f:
        for cfg in corpus["ground_truth"]:
            can_id = cfg["can_id"]
            dlc = cfg["dlc"]
            samples = corpus["samples"][str(can_id)]
            byte_labels = labels[str(can_id)]

            completion = build_completion_from_classifier(cfg, byte_labels)

            # jedno okno na configuracje (recentFrames + trigger), tak jak w
            # prawdziwym Cold Start; mozna latwo rozszerzyc na wiele okien
            # przesuwanych po probkach jesli zbior ma byc wiekszy.
            window = samples[:args.window]
            trigger = samples[args.window] if len(samples) > args.window else samples[-1]

            recent_str = format_frame_list(window, can_id, dlc)
            trigger_hex = "".join(f"{b:02X}" for b in trigger[:dlc])
            user_prompt = (
                f"Analyze the following CAN frame and suggest an interpretation:\n\n"
                f"CAN ID: 0x{can_id:03x} (DLC: {dlc})\n"
                f"Data bytes: ID=0x{can_id:03x} DLC={dlc} data=[{trigger_hex}]\n\n"
                f"Recent frames for this ID:\n{recent_str}"
            )

            example = {
                "messages": [
                    {"role": "system", "content": BASE_SYSTEM_PROMPT},
                    {"role": "user", "content": user_prompt},
                    {"role": "assistant", "content": json.dumps(completion)},
                ],
            }
            out_f.write(json.dumps(example) + "\n")
            n_examples += 1

    print(f"Zapisano {n_examples} przykladow treningowych do {args.out}")
    print("(format OpenAI fine-tuning JSONL - messages: system/user/assistant)")


if __name__ == "__main__":
    main()
