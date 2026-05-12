# CDManager Architecture

This project is intentionally split into layers early to keep feature growth manageable.

- `app/include/cdmanager/domain`
  Pure project and CD-TEXT entities with no UI or platform code.
- `app/include/cdmanager/application`
  Use-case helpers that transform or validate domain data.
- `app/include/cdmanager/infrastructure`
  Platform-aware implementations such as MS-JIS encoding, device access, and library adapters.
- `app/include/cdmanager/presentation`
  Qt widgets and view-facing code only.

中文约束:

- `domain` 只放稳定的业务对象，不放 Qt 窗口逻辑，不直接碰底层库。
- `application` 只负责编排流程和规则，不知道光驱细节来自哪个库。
- `infrastructure` 才允许碰 `libcdio`、`libburn`、系统设备枚举。
- `presentation` 只调 application 层，不直接读写设备。

Planned next modules:

- `infrastructure/disc`
  Wrappers for `libcdio`, `libburn`, and later `libisofs`.
- `application/import`
  Read disc/image/project data into the domain model.
- `application/burn`
  Prepare validated burn plans from domain data.
- `presentation/editor`
  Dedicated album and track editing widgets.
