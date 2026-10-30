"""ITP operational labels. Raw telemetry stays unchanged; not a science adapter.

Run with Azure CLI's bundled Python and its signed-in Entra identity.
Default is a read-only plan. --apply sets labels and publishes a versioned policy.
"""
import argparse
import base64
import collections
import datetime as dt
import hashlib
import json
from pathlib import Path

SUBSCRIPTION = "511ecd6d-4d4c-4322-9eba-e8d224e9b7dd"
ACCOUNT = "https://crosscloudresearch01.blob.core.windows.net"
RAW = "research-raw"
METADATA = "research-metadata"
TRIAL = "itp-2026-10-28-overnight-test"
BOUNDARY = "2026-10-27T16:22:35Z"
SOURCES = {"sensor-node-01", "sensor-node-02", "sensor-node-03"}


def utc(value):
    result = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    if result.tzinfo is None:
        raise ValueError("Timestamp must include its UTC offset")
    return result.astimezone(dt.timezone.utc)


def classify(record):
    source = record["source"]["device_id"]
    if source not in SOURCES:
        raise ValueError("Unexpected source")
    before = utc(record["gateway"]["received_at"]) < utc(BOUNDARY)
    flags = []
    if source == "sensor-node-02":
        flags.append("humidity_reference_check_pending")
    if source == "sensor-node-03" and before:
        flags.append("pre_sensor_type_correction")
    return ("setup" if before else "flagged_test"), flags


def self_test():
    sample = {"source": {"device_id": "sensor-node-03"},
              "gateway": {"received_at": "2026-10-27T16:22:34Z"}}
    assert classify(sample) == ("setup", ["pre_sensor_type_correction"])
    sample["gateway"]["received_at"] = BOUNDARY
    assert classify(sample) == ("flagged_test", [])
    sample["source"]["device_id"] = "sensor-node-02"
    assert classify(sample) == ("flagged_test", ["humidity_reference_check_pending"])
    for source, timestamp in [("unknown", BOUNDARY), ("sensor-node-01", "bad")]:
        sample["source"]["device_id"] = source
        sample["gateway"]["received_at"] = timestamp
        try:
            classify(sample)
        except (ValueError, KeyError):
            pass
        else:
            raise AssertionError("Unclassifiable record was accepted")
    print("PASS: boundary, source-specific flag and invalid-input cases")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--apply", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--output-dir", type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.output_dir is None:
        parser.error("--output-dir is required")
    args.output_dir.mkdir(parents=True, exist_ok=False)

    # Reuse the existing CLI session; no keys/SAS or access tokens are exported.
    from azure.cli.core._profile import Profile
    from azure.storage.blob import BlobServiceClient
    from azure.core import MatchConditions
    from azure.core.exceptions import ResourceExistsError
    credential, _, _ = Profile().get_login_credentials(subscription_id=SUBSCRIPTION)
    service = BlobServiceClient(ACCOUNT, credential=credential,
                               connection_timeout=20, read_timeout=30, retry_total=2)
    raw = service.get_container_client(RAW)
    properties_before = raw.get_container_properties()
    now = dt.datetime.now(dt.timezone.utc)
    stamp = now.strftime("%Y%m%dT%H%M%S%fZ")
    manifest_id = f"{TRIAL}-{stamp}"
    inventory = []
    for listed in raw.list_blobs():
        client = raw.get_blob_client(listed.name)
        properties = client.get_blob_properties()
        if properties.size > 10 * 1024 * 1024:
            raise ValueError("Large blob needs a separately reviewed streaming check")
        data = client.download_blob(etag=properties.etag,
                                    match_condition=MatchConditions.IfNotModified).readall()
        phases = collections.Counter()
        flags = collections.Counter()
        sources = collections.Counter()
        errors = []
        times = []
        for line_no, line in enumerate(data.decode("utf-8-sig").splitlines(), 1):
            if not line.strip():
                continue
            try:
                envelope = json.loads(line)
                body = envelope["Body"]
                record = json.loads(base64.b64decode(body, validate=True)) if isinstance(body, str) else body
                phase, record_flags = classify(record)
                phases[phase] += 1
                flags.update(record_flags)
                sources[record["source"]["device_id"]] += 1
                times.append(utc(record["gateway"]["received_at"]))
            except (ValueError, KeyError, TypeError) as error:
                errors.append({"line": line_no, "error": str(error)})
        phase = ("needs_review" if errors or not phases else
                 "mixed_setup_test" if len(phases) > 1 else next(iter(phases)))
        old_metadata = dict(properties.metadata)
        labels = dict(old_metadata)
        labels.update(itp_project="ITP", itp_phase=phase,
                      itp_quality="review_required", itp_label_set=TRIAL)
        if sum(len(k) + len(v) for k, v in labels.items()) > 7000:
            raise ValueError("Existing metadata leaves insufficient room")
        inventory.append({"name": listed.name, "etag": properties.etag,
                          "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                          "first_gateway_utc": min(times).isoformat() if times else None,
                          "last_gateway_utc": max(times).isoformat() if times else None,
                          "phase_counts": dict(phases), "source_counts": dict(sources),
                          "quality_flag_counts": dict(flags), "decode_errors": errors,
                          "phase": phase, "last_modified_before": properties.last_modified.isoformat(),
                          "previous_metadata": old_metadata, "proposed_metadata": labels,
                          "label_this_snapshot": (now - properties.last_modified).total_seconds() >= 180})

    policy = {
        "label_schema_version": "1.0", "project": "ITP", "label_set_id": TRIAL,
        "created_at_utc": now.isoformat(), "raw_account_url": ACCOUNT,
        "raw_container": RAW, "manifest_container": METADATA, "manifest_id": manifest_id,
        "manifest_location": "container metadata field label_policy_json",
        "collection_type": "setup_and_flagged_test", "formal_trial_start_utc": None,
        "time_field": "gateway.received_at", "boundary_utc": BOUNDARY,
        "boundary_malaysia": "2026-10-28T00:22:35+08:00",
        "rules": [
            {"when": "gateway.received_at < boundary_utc", "phase": "setup"},
            {"when": "gateway.received_at >= boundary_utc", "phase": "flagged_test"},
            {"when": "sensor-node-03 before boundary_utc", "flag": "pre_sensor_type_correction"},
            {"when": "sensor-node-02 at any time in this label set", "flag": "humidity_reference_check_pending"},
            {"when": "unrecognized source, missing or invalid timestamp/envelope", "phase": "needs_review"}],
        "source_configuration": {
            "sensor-node-01": {"configured_type": "DHT11", "description": "blue sensor"},
            "sensor-node-02": {"configured_type": "DHT11", "description": "blue sensor", "observed_humidity_percent": 88.6},
            "sensor-node-03": {"configured_type_after_boundary": "DHT22", "description": "white sensor; physical marking confirmation pending"}},
        "interpretation": [
            "Labels describe setup and quality review, not anomaly ground truth or calibrated accuracy.",
            "Mixed sensor models: do not describe as three identical DHT11 devices.",
            "The time rules also cover new blobs written after this snapshot until superseded by a newer label set.",
            "New blobs do not automatically inherit blob metadata labels. Apply timestamp rules after decoding raw data.",
            "Blob-level mixed_setup_test requires record-level time rules; do not classify the entire file as clean.",
            "Recent possibly active blobs are inventoried but not individually labelled in this one-time operation.",
            "Setting metadata changes blob ETag/last-modified properties; originals are retained in this snapshot. SHA-256 verifies unchanged raw content.",
            "Raw measurements, timestamps and validation flags remain unchanged; do not repair values or silently drop rows.",
            "Human approval is separate from dataset quality. This policy neither grants approval nor establishes a formal baseline."],
        "container_metadata_before": dict(properties_before.metadata), "blob_snapshot": inventory}
    encoded = (json.dumps(policy, indent=2) + "\n").encode("utf-8")
    (args.output_dir / "labels.json").write_bytes(encoded)
    if not args.apply:
        print(json.dumps({"mode": "read_only_plan", "blobs": len(inventory), "manifest_id": manifest_id}))
        return

    metadata = service.get_container_client(METADATA)
    try:
        metadata.create_container(metadata={"project": "ITP", "dataset_role": "experiment_labels"}, public_access=None)
    except ResourceExistsError:
        pass
    if metadata.get_container_properties().public_access is not None:
        raise ValueError("Metadata container must be private")
    # Container management is available to the existing project Owner. This
    # avoids granting data-plane rights on a new container merely to write a
    # manifest. The portable policy is readable directly in container metadata.
    portable_policy = {k: v for k, v in policy.items() if k not in ("blob_snapshot", "container_metadata_before")}
    portable_json = json.dumps(portable_policy, separators=(",", ":"), ensure_ascii=True)
    metadata_properties = metadata.get_container_properties()
    registry_labels = dict(metadata_properties.metadata)
    registry_labels.update(project="ITP", dataset_role="experiment_labels",
                           label_set_id=TRIAL, label_policy_json=portable_json,
                           label_set_version=stamp)
    if sum(len(k) + len(v) for k, v in registry_labels.items()) > 7000:
        raise ValueError("Portable policy exceeds conservative metadata budget")
    metadata.set_container_metadata(registry_labels, etag=metadata_properties.etag,
                                    match_condition=MatchConditions.IfNotModified)
    outcome = {"created_at_utc": now.isoformat(), "manifest_id": manifest_id,
               "labelled": [], "active_snapshot_not_labelled": [], "raw_payload_hash_mismatches": []}
    try:
        for item in inventory:
            if not item["label_this_snapshot"]:
                outcome["active_snapshot_not_labelled"].append(item["name"])
                continue
            client = raw.get_blob_client(item["name"])
            # Preserve metadata and reject a concurrent content/property change.
            client.set_blob_metadata(item["proposed_metadata"], etag=item["etag"],
                                     match_condition=MatchConditions.IfNotModified)
            after = client.get_blob_properties()
            if dict(after.metadata) != item["proposed_metadata"]:
                raise ValueError("Blob metadata read-back mismatch")
            unchanged = client.download_blob(etag=after.etag,
                                              match_condition=MatchConditions.IfNotModified).readall()
            if hashlib.sha256(unchanged).hexdigest() != item["sha256"]:
                outcome["raw_payload_hash_mismatches"].append(item["name"])
                raise ValueError("Raw content hash verification failed")
            outcome["labelled"].append({"name": item["name"], "phase": item["phase"],
                                        "etag_after": after.etag, "sha256_verified": True})
        current = raw.get_container_properties()
        container_labels = dict(current.metadata)
        container_labels.update(project="ITP", dataset_role="raw_telemetry",
                                dataset_phase="setup_and_flagged_test", formal_baseline="not_established",
                                label_policy_container=METADATA, label_policy_field="label_policy_json",
                                label_set_id=TRIAL, label_set_version=stamp,
                                node2_humidity="reference_check_pending", node3_corrected_from_utc=BOUNDARY)
        raw.set_container_metadata(container_labels, etag=current.etag,
                                   match_condition=MatchConditions.IfNotModified)
        if dict(raw.get_container_properties().metadata) != container_labels:
            raise ValueError("Container metadata verification failed")
        if metadata.get_container_properties().metadata.get("label_policy_json") != portable_json:
            raise ValueError("Policy metadata verification failed")
        outcome["container_metadata_verified"] = True
        outcome["policy_metadata_verified"] = True
        outcome["raw_payloads_modified_by_script"] = False
    finally:
        (args.output_dir / "deployment-result.json").write_text(json.dumps(outcome, indent=2), encoding="utf-8")
    print(json.dumps({"labelled_blobs": len(outcome["labelled"]),
                      "active_blobs_skipped": len(outcome["active_snapshot_not_labelled"]),
                      "container_metadata_verified": outcome.get("container_metadata_verified", False),
                      "policy_metadata_verified": outcome.get("policy_metadata_verified", False),
                      "local_evidence": str(args.output_dir), "manifest_id": manifest_id}, indent=2))


if __name__ == "__main__":
    main()
