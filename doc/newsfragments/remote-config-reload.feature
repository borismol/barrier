The server can now load its configuration from a URL with ``--config-url`` and
reload it remotely by sending the reserved ``reloadConfig|`` command to the
external command channel on 127.0.0.1 (``--external-command-port``).  An
auxiliary file can be kept in sync on every reload with ``--download-url`` and
``--download-path``.
