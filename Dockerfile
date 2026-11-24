# A ready-made Linux toolbox for the course: compiler, debugger, strace, man
# pages, and the HTTP tools the docs use. Your source is not copied in; the repo
# is mounted at /work at run time (see compose.yaml), so edits persist on the host.

FROM ubuntu:24.04

# Build as your host user so files created in /work (build/, your edits) are
# owned by you, not root. compose.yaml passes these through.
ARG USER_UID=1000
ARG USER_GID=1000

ENV DEBIAN_FRONTEND=noninteractive

# The Ubuntu base image is "minimized": it strips man pages and replaces `man`
# with a stub. This course leans on `man 2 write` and friends, so undo both
# before installing anything.
RUN rm -f /etc/dpkg/dpkg.cfg.d/excludes \
 && rm -f /usr/bin/man \
 && dpkg-divert --quiet --local --rename --remove /usr/bin/man \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential gdb strace ltrace \
      python3 \
      curl netcat-openbsd socat iproute2 procps lsof \
      man-db manpages manpages-dev \
      linux-tools-generic \
      git less nano vim-tiny sudo ca-certificates bash-completion \
 && rm -rf /var/lib/apt/lists/* \
 && ln -s "$(ls -d /usr/lib/linux-tools/*/perf | head -n1)" /usr/local/bin/perf \
 && mandb -q

# Replace the image's default `ubuntu` user (UID 1000) with `dev` at your UID.
RUN (userdel -r ubuntu 2>/dev/null || true) \
 && groupadd -o -g "${USER_GID}" dev \
 && useradd -o -m -u "${USER_UID}" -g "${USER_GID}" -s /bin/bash dev \
 && echo 'dev ALL=(ALL) NOPASSWD:ALL' > /etc/sudoers.d/dev \
 && chmod 0440 /etc/sudoers.d/dev \
 && git config --system --add safe.directory /work

USER dev
WORKDIR /work
CMD ["bash"]
