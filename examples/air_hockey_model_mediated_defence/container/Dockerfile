# syntax=docker/dockerfile:1.7
FROM marvin/drl-air-hockey:2025-a41081c4c386-blackwell-rebuilt@sha256:de846e25961d2408db73a6b5cfe2afc7ecf0e14e0e8ac837f97212a180e51d5f

ARG ACRA_REVISION
ARG MUESLI_BT_REVISION

LABEL org.opencontainers.image.title="muesli air-hockey integration"
LABEL org.opencontainers.image.revision="${MUESLI_BT_REVISION}"
LABEL org.muesli-bt.air-hockey.acra-revision="${ACRA_REVISION}"

ENV PYTHONPATH="/opt/airhockey-distillation/src:/opt/muesli-bt/examples/air_hockey_model_mediated_defence:/src/2025-challenge"

COPY --from=airhockey_distill / /opt/airhockey-distillation
COPY . /opt/muesli-bt

RUN test "${ACRA_REVISION}" = "1b6bbbbf19743b0042f01eabf0628eba5621cacf" && \
    test "$(cat /opt/airhockey-distillation/.air-hockey-source-revision)" = "${ACRA_REVISION}" && \
    test -n "${MUESLI_BT_REVISION}" && \
    test "$(cat /opt/muesli-bt/.muesli-bt-source-revision)" = "${MUESLI_BT_REVISION}"

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
      cmake=3.28.3-1build7 \
      g++=4:13.2.0-7ubuntu1 \
      make=4.3-4.1build2 && \
    rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --no-cache-dir \
      -r /opt/muesli-bt/examples/air_hockey_model_mediated_defence/container/requirements-wp4.txt && \
    python3 -m pip install --no-build-isolation --no-deps /opt/airhockey-distillation

RUN cmake -S /opt/muesli-bt -B /opt/muesli-bt/build/air-hockey-wp4 && \
    cmake --build /opt/muesli-bt/build/air-hockey-wp4 \
      --target muesli_bt_air_hockey_scenario_tests -j2

RUN python3 /opt/muesli-bt/examples/air_hockey_model_mediated_defence/run_wp4.py \
      container-check

WORKDIR /opt/muesli-bt
CMD ["python3", "/opt/muesli-bt/examples/air_hockey_model_mediated_defence/run_g2.py", "--runner", "/opt/muesli-bt/build/air-hockey-wp4/muesli_bt_air_hockey_scenario_tests", "--scenario", "H1"]
