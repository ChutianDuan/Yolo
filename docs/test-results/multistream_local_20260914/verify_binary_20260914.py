# Temporary read-only audit for the frozen local independent request-pool evidence.
import json,pathlib,math,hashlib
root=pathlib.Path(__file__).resolve().parent
rows=[]
source_sha256={}
reference_runtime=None
reference_config=None
reference_models=None
reference_thresholds=None
reference_host=None
previous_config=None
reference_actual_runtime=None
build=json.loads((root/"binary_build_20260914.json").read_text())
assert build["unchanged_after_cases"] and build["changed_object_count"]==1
assert build["before"]==build["after"]
assert len(build["before"]["objects"])==build["common_object_count"]+1==23
assert build["before"]["objects"][build["swapped_object"]]!=build["before"]["reference_object_sha256"]
assert hashlib.sha256((root/build["reference_source_snapshot"]).read_bytes()).hexdigest()==build["before"]["reference_source_sha256"]
assert hashlib.sha256((root.parents[2]/"yolo_onnx_cpp/video/optical_flow_tracker.cpp").read_bytes()).hexdigest()==build["before"]["source_sha256"]
for directory,hreq,lreq,mode,cv_threads in (("diag57_six_original_binary",1,2,"throughput",4),
                                             ("diag57_six_current_binary",1,2,"throughput",4)):
    name=directory
    high=low=16
    count=6
    p=root/directory
    assert (p/"diagnostic.json").exists(), "missing matrix case: "+name
    report=json.loads((p/"acceptance/acceptance.json").read_text())
    manifest=json.loads((p/"diagnostic.json").read_text())
    timing=json.loads((p/"timing_metrics.json").read_text())
    config=(p/"diagnostic-config.yaml").read_text()
    assert config.splitlines().count("max_streams: "+str(count))==1
    assert manifest["configured_high_threads"]==high and manifest["configured_low_threads"]==low
    assert manifest["configured_high_requests"]==hreq and manifest["configured_low_requests"]==lreq
    assert manifest["performance_mode"]==mode
    assert manifest["configured_cpu_pinning"]=="false" and manifest["configured_low_detect_fps"]==5
    for item in manifest["inputs"]:
        assert (item["width"],item["height"],item["cached_frames"])==(1280,720,180)
        key=item["basename"]
        assert source_sha256.get(key,item["sha256"])==item["sha256"]
        source_sha256[key]=item["sha256"]
    lines=config.splitlines()
    for key,value in (("high_model_threads",high),("low_model_threads",low),("opencv_threads",cv_threads),
                      ("infer_request_count",hreq),("low_res_infer_request_count",lreq)):
        assert lines.count(key+": "+str(value))==1
    if previous_config is not None:
        assert len(lines)==len(previous_config)
        changed=[(a,b) for a,b in zip(previous_config,lines) if a!=b]
        assert len(changed)==0
    previous_config=lines
    normalized="\n".join(x for x in lines if not x.startswith(("infer_request_count:","low_res_infer_request_count:","openvino_performance_mode:","opencv_threads:")))
    runtime=json.loads(json.dumps(manifest["runtime_records"]))
    actual_runtime=json.loads(json.dumps(runtime))
    if reference_actual_runtime is None:
        reference_actual_runtime=actual_runtime
    assert actual_runtime==reference_actual_runtime
    assert len(runtime)==2
    for record,threads,requests,shape in zip(runtime,(high,low),(hreq,lreq),((1280,736),(640,384))):
        assert (record["input_width"],record["input_height"])==shape
        assert record["backend"]=="openvino" and record["runtime_build"]=="2026.1.0-000--"
        assert record.pop("request_pool_size")==requests
        props=record["properties"]
        assert props.pop("INFERENCE_NUM_THREADS")==str(threads)
        assert props.pop("PERFORMANCE_HINT_NUM_REQUESTS")==str(requests)
        assert int(props.pop("NUM_STREAMS"))>=1
        assert int(props.pop("OPTIMAL_NUMBER_OF_INFER_REQUESTS"))>=1
        assert (props["EXECUTION_DEVICES"],props["INFERENCE_PRECISION_HINT"],
                props["ENABLE_CPU_PINNING"],props["ENABLE_HYPER_THREADING"])==("CPU","f32","NO","NO")
    models=[a["sha256"] for a in report["provenance"]["artifacts"] if a["kind"]=="model"]
    thresholds=report["thresholds"]
    host_keys=("cpu_models","logical_cpus","physical_cores","sockets",
               "collector_cpu_affinity","monitored_pid_cpu_affinity")
    host={k:report["provenance"]["collector_host"].get(k) for k in host_keys}
    assert (thresholds["min_processed_fps"],thresholds["min_low_detection_fps"],
            thresholds["min_high_detection_fps"],thresholds["max_result_age_ms"])==(25,4,.5,300)
    assert (thresholds["max_cpu_percent"],thresholds["max_memory_growth_percent"],
            thresholds["max_fairness_spread_percent"],thresholds["max_queue_depth"])==(85,5,10,2)
    if reference_runtime is None:
        reference_runtime,reference_config,reference_models=runtime,normalized,models
        reference_thresholds,reference_host=thresholds,host
    assert thresholds==reference_thresholds and host==reference_host
    assert runtime==reference_runtime and normalized==reference_config and models==reference_models
    assert report["requested_stream_count"]==count and len(report["streams"])==count
    assert report["sample_count"]==31 and report["elapsed_seconds"]>=30
    assert report["sampling_warmup"]["completed"] and not report["sampling_warmup"]["errors"]
    assert all(a["unchanged"] and a["sha256"]==a["sha256_after"] for a in report["provenance"]["artifacts"])
    assert not report["run_errors"] and not timing["errors"] and manifest["server_returncode"]==0
    assert manifest["collector_returncode"]==(0 if report["overall_status"]=="PASS" else 2)
    parsed=[]
    for sample in timing["samples"]:
        metrics={}
        for line in sample["prometheus_text"].splitlines():
            if line and not line.startswith("#") and "stream_id=" not in line:
                key,value=line.rsplit(" ",1)
                metrics[key]=float(value)
        parsed.append(metrics)
    selected=[k for k in parsed[0] if k.startswith("yolo_stream_async_") or k.startswith("yolo_scheduler_updated_total") or k.startswith("yolo_scheduler_cancelled_total") or k.startswith("yolo_stream_processing_stage_")]
    assert len(selected)==78
    assert 35<=len(parsed)<=45
    assert manifest["timing_metrics_sample_count"]==len(parsed)
    assert all(b["monotonic_seconds"]>a["monotonic_seconds"] for a,b in zip(timing["samples"],timing["samples"][1:]))
    assert all(math.isfinite(m[k]) and m[k]>=0 for m in parsed for k in selected)
    assert all(b[k]>=a[k] for a,b in zip(parsed,parsed[1:]) for k in selected)
    final=parsed[-1]
    clean_keys=["yolo_streams_active","yolo_streams_registered"]
    clean_keys += [f'yolo_scheduler_{kind}{{tier="{tier}"}}' for tier in ("high","low") for kind in ("queue_depth","in_flight")]
    assert final["yolo_streams_active"]==0 and final["yolo_streams_registered"]==0
    assert all(final[f'yolo_scheduler_queue_depth{{tier="{tier}"}}']==0 for tier in ("high","low"))
    cleanup_gauges={k:final[k] for k in clean_keys}
    processing={}
    for stage_name in ("frame_work","prepare","poll","publish"):
        suffix='{stage="'+stage_name+'"}'
        n=final["yolo_stream_processing_stage_seconds_count"+suffix]
        total=final["yolo_stream_processing_stage_seconds_sum"+suffix]
        maximum=final["yolo_stream_processing_stage_seconds_max"+suffix]
        assert n>0 and total>=maximum>=0
        processing[stage_name]={"count":int(n),"sum_ms":1000*total,
                               "mean_ms":1000*total/n,"max_ms":1000*maximum}
    assert processing["frame_work"]["count"]==processing["poll"]["count"]
    assert processing["prepare"]["count"]<=processing["frame_work"]["count"]
    assert processing["publish"]["count"]<=processing["prepare"]["count"]
    assert processing["frame_work"]["sum_ms"]>=sum(processing[k]["sum_ms"] for k in ("prepare","poll","publish"))
    processing_by_stream={}
    for snapshot in report["streams"]:
        sid=snapshot["stream_id"]
        keys={stage_name:'yolo_stream_processing_stage_by_stream_seconds_count{stage="'+stage_name+'",stream_id="'+sid+'"}'
              for stage_name in processing}
        # Per-stream series disappear on removal; use the last registered scrape for this ID.
        registered=[sample for sample in timing["samples"] if all(key+" " in sample["prometheus_text"] for key in keys.values())]
        assert registered
        values={line.rsplit(" ",1)[0]:float(line.rsplit(" ",1)[1]) for line in registered[-1]["prometheus_text"].splitlines()
                if line and not line.startswith("#")}
        row={}
        for stage_name,key in keys.items():
            n=values[key]
            assert n>0
            row[stage_name]={"count":int(n),
                "mean_ms":1000*values[key.replace("_count{","_sum{")]/n,
                "max_ms":1000*values[key.replace("_count{","_max{")]}
        processing_by_stream[sid]={"last_registered_monotonic_seconds":registered[-1]["monotonic_seconds"],
                                  "processing":row}
    stages={}
    for tier in ("high","low"):
        def outcome(kind: str) -> int:
            return int(final[f'yolo_stream_async_results_total{{tier="{tier}",outcome="{kind}"}}'])
        stage={"completed":outcome("completed"),"applied":outcome("applied"),
               "expired":outcome("expired"),"evicted":outcome("evicted")}
        assert stage["completed"]==stage["applied"]+stage["expired"]+stage["evicted"]
        for kind in ("queue_wait","infer","result_age","replay","commit_age"):
            prefix=f'{{tier="{tier}",stage="{kind}"}}'
            n=final["yolo_stream_async_stage_seconds_count"+prefix]
            stage[kind+"_mean_ms"]=1000*final["yolo_stream_async_stage_seconds_sum"+prefix]/n if n else None
        stage["updated"]=int(final[f'yolo_scheduler_updated_total{{tier="{tier}"}}'])
        sched={kind:final[f'yolo_scheduler_{kind}{{tier="{tier}"}}'] for kind in
               ("submitted_total","completed_total","replaced_total","stale_total","failed_total","cancelled_total","queue_depth","in_flight")}
        assert sched["submitted_total"]==sum(sched[k] for k in
               ("completed_total","replaced_total","stale_total","failed_total","cancelled_total","queue_depth","in_flight"))
        assert sched["failed_total"]==0
        stage["scheduler"]=sched
        stages[tier]=stage
    streams=report["streams"]
    def extent(k: str) -> list[float]: return [min(s[k] for s in streams),max(s[k] for s in streams)]
    assert all(s["inference_errors_max"]==0 and s["reconnects_delta"]==0 for s in streams)
    assert len(manifest["source_connections"])==count
    assert {s["path"] for s in manifest["source_connections"]}=={
        "/camera/"+str(i)+".mjpg" for i in range(1,count+1)}
    assert manifest["source_content_reused"]==(count>4)
    assert manifest["distinct_source_contents"]==min(count,4)
    expected_binary=build["before"]["reference_binary_sha256" if directory.endswith("original_binary") else "current_binary_sha256"]
    assert manifest["actual_executable_verified"] is True
    assert manifest["service_binary_sha256"]==manifest["service_binary_sha256_after"]==expected_binary
    assert manifest["service_binary"]==("original_api57" if directory.endswith("original_binary") else "yolo_api")
    memory=timing["memory_samples"]
    assert len(memory)==len(parsed)==manifest["memory_sample_count"]
    allowed={k+"_kib" for k in ("Rss","Pss","Private_Clean","Private_Dirty","Shared_Clean","Shared_Dirty","Anonymous","AnonHugePages")}
    assert all(set(m)==allowed|{"threads","monotonic_seconds"} for m in memory)
    assert all(type(m[k]) is int and m[k]>=0 for m in memory for k in allowed|{"threads"})
    assert all(math.isfinite(m["monotonic_seconds"]) for m in memory)
    assert all(b["monotonic_seconds"]>a["monotonic_seconds"] for a,b in zip(memory,memory[1:]))
    assert all(0<=m["monotonic_seconds"]-s["monotonic_seconds"]<2 for m,s in zip(memory,timing["samples"]))
    memory_trace={k:{"first":memory[0][k],"last":memory[-1][k],"min":min(m[k] for m in memory),"peak":max(m[k] for m in memory)} for k in sorted(allowed|{"threads"})}
    rows.append({"service_binary_sha256":expected_binary,"memory_trace_full_lifecycle":memory_trace,"count":count,"high_threads":high,"low_threads":low,"opencv_threads":cv_threads,
        "high_requests":hreq,"low_requests":lreq,"performance_mode":mode,"runtime":actual_runtime,"case":p.name,"status":report["overall_status"],
        "elapsed_seconds":report["elapsed_seconds"],"warmup_seconds":report["sampling_warmup"]["elapsed_seconds"],
        "output_fps":extent("processed_fps"),"low_fps":extent("low_detection_fps"),
        "high_fps":extent("high_detection_fps"),"p95_ms":extent("result_age_ms_p95"),
        "fairness_percent":report["fairness_spread_percent"],
        "cpu_host_percent":report["process"]["cpu_average_percent_of_host"],
        "rss_growth_percent":report["process"]["rss_growth_percent"],
        "failed_gates":[g["name"] for g in report["gates"] if g["passed"] is not True],
        "metrics_samples":len(parsed),"monotonic_global_series":78,"service_exited_and_streams_removed":True,
        "post_delete_gauges":cleanup_gauges,
        "distinct_source_contents":manifest["distinct_source_contents"],
        "source_fps":[min(s["delivered_fps"] for s in manifest["source_connections"]),
                      max(s["delivered_fps"] for s in manifest["source_connections"])],
        "processing_global":processing,"processing_by_stream":processing_by_stream,"stages":stages})
assert len(rows)==2 and len(source_sha256)==4
print(json.dumps({"purpose":"single swapped flow-object original/current binary comparison; one ordered pair is not a statistically validated speedup or RSS root-cause proof; memory trace includes startup and retirement, not formal-window RSS gate; not RTSP/NUMA/soak/quality acceptance",
                 "controls_verified":True,"runtime_without_request_stream_properties":reference_runtime,"model_sha256":reference_models,"source_sha256":source_sha256,
                 "rows":rows},ensure_ascii=False))
