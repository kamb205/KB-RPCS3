# Permission requests (drafts, not yet sent)

A KB-RPCS3 PS5 binary can be distributed only if every non-RPCS3 component linked into it is available
under terms compatible with RPCS3's GPL-2.0-only (see `LICENSING_STATUS.md` §4). These drafts ask the
copyright holders of the GPL-3.0 components for that.

## 1. Mihawk — PS5_Vulkan, PS5 Vulkan Template, PS5 platform layer, PS5_VKHomebrewUI fork

> Subject: Permission to use PS5_Vulkan / the PS5 Vulkan Template with GPL-2.0-only RPCS3
>
> Hello Mihawk,
>
> I maintain KB-RPCS3, an unofficial RPCS3 port for PS5 homebrew, built on your PS5_Vulkan stack and
> the PS5 Vulkan Template. RPCS3 is GPL-2.0-only, and these components of yours are linked into the same
> eboot.bin:
>
> - the PS5 Vulkan Template UI module (`ps5/ui/*`) and title program base (GPL-3.0-or-later);
> - PS5_Vulkan's `app_crt.cpp` and the `libc.prx` module shipped with titles;
> - the PS5 platform layer in your payload-SDK fork (`libps5platform.a`).
>
> Because GPL-2.0-only and GPL-3.0 cannot be combined, I am publishing source only and no binary.
> Would you be willing to make these components additionally available under terms compatible with
> GPL-2.0-only, for example GPL-2.0-or-later or MIT? Your copyright notices and credit stay in every
> file and in the release notes. I can send exact file lists and revisions.
>
> Thank you for the PS5 Vulkan work — KB

## 2. BlackBearReloaded — ps5-homebrew-ui

> Subject: Permission to use the ps5-homebrew-ui kit with GPL-2.0-only RPCS3
>
> Hello BlackBearReloaded,
>
> KB-RPCS3, an unofficial RPCS3 port for PS5 homebrew, draws its home screen with your ps5-homebrew-ui
> kit (through Mihawk's PS5_VKHomebrewUI fork). The kit code is GPL-3.0, and RPCS3 is GPL-2.0-only, so a
> combined binary cannot be distributed today.
>
> Would you be willing to make the kit code additionally available under terms compatible with
> GPL-2.0-only (for example GPL-2.0-or-later or MIT)? If you prefer, the sounds and music can be left
> out and replaced; only the code matters for the binary. Your credit stays in the source and the
> release notes.
>
> Thank you — KB

## 3. ps5-payload-dev — ps5-payload-sdk

> Subject: Which SDK objects end up in a PS5 title, and under what terms?
>
> Hello,
>
> KB-RPCS3 links a PS5 title with a toolchain derived from ps5-payload-sdk (v0.42). To check
> compatibility with GPL-2.0-only RPCS3: which SDK objects are linked into a title (startup code, stub
> libraries, code from headers), and are they covered by the SDK's GPL-3.0-or-later or by other terms?
>
> Thanks — KB

## Status

| Request | Sent | Answer |
|---|---|---|
| Mihawk | no | — |
| BlackBearReloaded | no | — |
| ps5-payload-dev | no | — |
| KB's own code under GPL-2.0-or-later as well | maintainer decision pending | — |
