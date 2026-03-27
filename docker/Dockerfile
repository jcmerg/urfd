## ----- Build Stage -----
FROM ubuntu:24.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    g++ \
    make \
    libcurl4-gnutls-dev \
    libopendht-dev \
    nlohmann-json3-dev \
    libopus-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY reflector/ ./reflector/
COPY config/urfd.mk ./reflector/urfd.mk

WORKDIR /build/reflector
RUN make clean 2>/dev/null; make -j$(nproc)

## ----- Runtime Stage -----
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libcurl3t64-gnutls \
    libopendht3t64 \
    libopus0 \
    nginx \
    php-fpm \
    supervisor \
    curl \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /var/run/php /var/log/supervisor /var/lib/xlxd-ch /usr/local/etc/urfd

# Copy urfd binary and tools
COPY --from=builder /build/reflector/urfd /usr/local/bin/urfd
COPY --from=builder /build/reflector/inicheck /usr/local/bin/inicheck
COPY --from=builder /build/reflector/dbutil /usr/local/bin/dbutil

# Copy dashboard
COPY dashboard/ /var/www/urfd-dashboard/
RUN chown -R www-data:www-data /var/www/urfd-dashboard /var/lib/xlxd-ch

# Supervisord and nginx configs are injected at build time via --build-context
COPY --from=optfiles supervisord.conf /etc/supervisor/conf.d/urfd.conf
COPY --from=optfiles nginx-dashboard.conf /etc/nginx/sites-available/urfd-dashboard.conf
RUN rm -f /etc/nginx/sites-enabled/default \
    && ln -sf /etc/nginx/sites-available/urfd-dashboard.conf /etc/nginx/sites-enabled/urfd-dashboard.conf

# Ensure XML file exists for dashboard
RUN touch /var/log/xlxd.xml && chown root:root /var/log/xlxd.xml

HEALTHCHECK --interval=30s --timeout=5s --retries=3 \
    CMD pgrep urfd > /dev/null && curl -sf http://127.0.0.1:8363/ > /dev/null

CMD ["/usr/bin/supervisord", "-n", "-c", "/etc/supervisor/conf.d/urfd.conf"]
