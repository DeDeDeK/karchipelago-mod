# https://devkitpro.org/wiki/devkitPro_pacman

FROM fedora:latest

RUN dnf install -y pacman

RUN pacman-key --init

ENV DEVKITPRO=/opt/devkitpro \
    DEVKITARM=/opt/devkitpro/devkitARM \
    DEVKITPPC=/opt/devkitpro/devkitPPC

RUN pacman-key --recv BC26F752D25B92CE272E0F44F7FD5492264BB9D0 --keyserver keyserver.ubuntu.com

RUN pacman-key --lsign BC26F752D25B92CE272E0F44F7FD5492264BB9D0

RUN pacman -U --noconfirm https://pkg.devkitpro.org/devkitpro-keyring.pkg.tar.zst

RUN pacman-key --populate devkitpro

RUN echo -e "\n[dkp-libs]\nServer = https://pkg.devkitpro.org/packages\n\n[dkp-linux]\nServer = https://pkg.devkitpro.org/packages/linux/\$arch/" >> /etc/pacman.conf

RUN pacman -Syu --noconfirm

# installs to /opt/devkitpro
RUN pacman -S --noconfirm gamecube-dev
