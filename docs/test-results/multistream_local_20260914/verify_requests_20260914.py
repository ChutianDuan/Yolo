# Temporary read-only audit for the frozen local independent request-pool evidence.
import json,pathlib,math
root=pathlib.Path(__file__).resolve().parent
rows=[]
source_sha256={}
reference_runtime=None
reference_config=None
reference_models=None
reference_thresholds=None
reference_host=None
previous_config=None
for name,hreq,lreq,mode,cv_threads in (("latency_h1_l1",1,1,"latency",4),("latency_h1_l2",1,2,"latency",4),
                                       ("throughput_h1_l2",1,2,"throughput",4),("throughput_h1_l2_cv2",1,2,"throughput",2)):
    high=low=16
    count=6
    p=root/("diag54_six_"+name)
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
        assert len(changed)==1
    previous_config=lines
    normalized="\n".join(x for x in lines if not x.startswith(("infer_request_count:","low_res_infer_request_count:","openvino_performance_mode:","opencv_threads:")))
    runtime=json.loads(json.dumps(manifest["runtime_records"]))
    actual_runtime=json.loads(json.dumps(runtime))
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
    selected=[k for k in parsed[0] if k.startswith("yolo_stream_async_") or k.startswith("yolo_scheduler_updated_total") or k.startswith("yolo_scheduler_cancelled_total")]
    assert len(selected)==66
    assert len(parsed)==41
    assert all(b["monotonic_seconds"]>a["monotonic_seconds"] for a,b in zip(timing["samples"],timing["samples"][1:]))
    assert all(math.isfinite(m[k]) and m[k]>=0 for m in parsed for k in selected)
    assert all(b[k]>=a[k] for a,b in zip(parsed,parsed[1:]) for k in selected)
    final=parsed[-1]
    clean_keys=["yolo_streams_active","yolo_streams_registered"]
    clean_keys += [f'yolo_scheduler_{kind}{{tier="{tier}"}}' for tier in ("high","low") for kind in ("queue_depth","in_flight")]
    assert final["yolo_streams_active"]==0 and final["yolo_streams_registered"]==0
    assert all(final[f'yolo_scheduler_queue_depth{{tier="{tier}"}}']==0 for tier in ("high","low"))
    cleanup_gauges={k:final[k] for k in clean_keys}
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
    rows.append({"count":count,"high_threads":high,"low_threads":low,"opencv_threads":cv_threads,
        "high_requests":hreq,"low_requests":lreq,"performance_mode":mode,"runtime":actual_runtime,"case":p.name,"status":report["overall_status"],
        "elapsed_seconds":report["elapsed_seconds"],"warmup_seconds":report["sampling_warmup"]["elapsed_seconds"],
        "output_fps":extent("processed_fps"),"low_fps":extent("low_detection_fps"),
        "high_fps":extent("high_detection_fps"),"p95_ms":extent("result_age_ms_p95"),
        "fairness_percent":report["fairness_spread_percent"],
        "cpu_host_percent":report["process"]["cpu_average_percent_of_host"],
        "rss_growth_percent":report["process"]["rss_growth_percent"],
        "failed_gates":[g["name"] for g in report["gates"] if g["passed"] is not True],
        "metrics_samples":len(parsed),"monotonic_global_series":66,"service_exited_and_streams_removed":True,
        "post_delete_gauges":cleanup_gauges,
        "distinct_source_contents":manifest["distinct_source_contents"],
        "source_fps":[min(s["delivered_fps"] for s in manifest["source_connections"]),
                      max(s["delivered_fps"] for s in manifest["source_connections"])],
        "stages":stages})
assert len(rows)==4 and len(source_sha256)==4
print(json.dumps({"purpose":"single-variable request-pool/performance-hint local short-window evidence, not RTSP/NUMA/soak/quality acceptance",
                 "controls_verified":True,"runtime_without_request_stream_properties":reference_runtime,"model_sha256":reference_models,"source_sha256":source_sha256,
                 "rows":rows},ensure_ascii=False))
