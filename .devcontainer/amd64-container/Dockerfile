FROM effectiverange/er-devc-amd64:latest

ARG USERNAME=$USER
ARG USER_UID=1000
ARG USER_GID=$USER_UID

# [Optional] Uncomment this section to install additional vcpkg ports.
# RUN su vscode -c "${VCPKG_ROOT}/vcpkg install <your-port-name-here>"

# Create the user and get ownership of /opt/cross for package installation
RUN groupadd --gid $USER_GID $USERNAME \
    && useradd -s /bin/bash --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME && chown -R $USERNAME:$USERNAME /opt/debs /var/chroot/buildroot \
    && sed -ri "s/(users=.*)/\1,$USERNAME/g" /etc/schroot/chroot.d/buildroot

# ********************************************************
# * Anything else you want to do like clean up goes here *
# ********************************************************

# [Optional] Set the default user. Omit if you want to keep the default as root.
USER $USERNAME    
