# Shadow Recompiled UWP

Port não oficial de **Shadow Recompiled** para **Xbox Series X|S**, executado no Modo de Desenvolvedor. A versão 1.0.9.0 foi testada no console com o jogo em armazenamento externo, incluindo as abas **Opções** e **Conquistas** no menu de pausa.

[Baixar a versão mais recente](https://github.com/mateusmol-bruh/ShadowRecomp-UWP/releases/latest) · [Instalar no Xbox](#instalar-no-xbox) · [Preparar o HD externo](#preparar-o-hd-externo) · [Compilar](#compilar)

## O que você precisa

- Xbox Series X ou Series S com o Modo de Desenvolvedor ativado. [Como ativar o Modo de Desenvolvedor](https://learn.microsoft.com/pt-br/previous-versions/windows/uwp/xbox-apps/devkit-activation)
- Um PC na mesma rede local do console para instalar o pacote pelo Xbox Device Portal.
- Uma cópia do **Shadow the Hedgehog (2005) para o Xbox original**. Você pode usar o disco extraído com `default.xbe` e os demais arquivos, ou uma imagem ISO compatível.
- Para jogar pelo USB, um HD, SSD ou pendrive em **NTFS** com acesso de gravação para apps do Xbox.

O pacote do port precisa dos arquivos da sua própria cópia do jogo. Mantenha o `default.xbe` junto dos demais arquivos do disco extraído.

## Baixar os arquivos

Na [página da versão](https://github.com/mateusmol-bruh/ShadowRecomp-UWP/releases/latest), abra **Assets** e baixe:

| Arquivo | Uso |
| --- | --- |
| `ShadowRecomp.UWP.x64.appx` | Pacote do aplicativo para instalar no Xbox. |
| `Microsoft.VCLibs.x64.14.00.appx` | Dependência x64 da Microsoft. Selecione-a quando o portal pedir dependências. |
| `ShadowRecomp-UWP.cer` | Certificado público da assinatura do pacote. Use-o se o portal solicitar o certificado. |
| `storage.txt` | Seletor USB pronto para um dispositivo com o identificador `shadow-usb`. |

SDL3 e DirectX Shader Compiler já integram o APPX principal.

## Preparar o HD externo

1. No PC, crie a pasta **`shadow05`** na raiz da unidade NTFS. Nas propriedades de segurança dessa pasta, conceda **Modificar** a **Todos os Pacotes de Aplicativos** (`ALL APPLICATION PACKAGES`), incluindo arquivos e subpastas.
2. Crie `shadow05/storage-id.txt` com uma única linha, por exemplo `shadow-usb`. Use até 64 letras ASCII, números, hífens ou sublinhados.
3. Crie `shadow05/original/` e copie para lá o disco extraído inteiro. A raiz de `original/` deve conter `default.xbe` e as pastas do disco. Também é possível colocar o disco inteiro em uma subpasta imediata de `original/`.
4. Conecte o HD ao Xbox. Se o console perguntar como usar a unidade, escolha **Mídia**.

```text
X:/
└── shadow05/
    ├── storage-id.txt
    ├── original/
    │   ├── default.xbe
    │   ├── common/
    │   ├── sound/
    │   └── demais arquivos e pastas do disco
    ├── save/
    └── mods/
```

O app cria `save/` e `mods/` no armazenamento selecionado. Você pode preparar a pasta e aplicar as permissões pelo PowerShell no PC. Substitua `X:` pela letra real do seu HD.

```powershell
New-Item -ItemType Directory -Path 'X:\shadow05\original' -Force
icacls 'X:\shadow05' /grant '*S-1-15-2-1:(OI)(CI)M' /T
Set-Content -Encoding ascii -LiteralPath 'X:\shadow05\storage-id.txt' -Value 'shadow-usb'
```

Para usar uma **ISO**, copie a imagem para `shadow05/original/` e siga o assistente ao abrir o app. Ele extrai os dados para `shadow05/game/`, que exige espaço adicional. O disco extraído pode ser aberto diretamente após a verificação.

## Instalar no Xbox

1. No Xbox em Modo de Desenvolvedor, abra o **Dev Home**. Em **Remote Access Settings**, habilite o **Xbox Device Portal** e abra no navegador do PC o endereço exibido no console.
2. Na aba **Home** do portal, use **Add** em **My games & apps** e selecione `ShadowRecomp.UWP.x64.appx`.
3. Na etapa de dependências, adicione `Microsoft.VCLibs.x64.14.00.appx`. Se a interface pedir um certificado, selecione `ShadowRecomp-UWP.cer`. Conclua a instalação.
4. Abra **Shadow Recompiled** pelo Dev Home com o HD conectado. Com um único USB preparado e sem instalação interna anterior, o app seleciona a pasta `shadow05` automaticamente.
5. Para escolher um HD específico ou migrar de uma instalação interna, feche o app e envie `storage.txt` pelo **File Explorer → LocalAppData → Shadow Recompiled → LocalState** do portal. O arquivo deve conter `usb:shadow-usb`. Substitua `shadow-usb` pelo conteúdo de `storage-id.txt` se escolheu outro identificador. Abra o jogo novamente.

[Implantação manual de APPX no Xbox](https://learn.microsoft.com/en-us/windows/uwp/apps-for-xbox/development-options) · [Explorador de arquivos do Device Portal](https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal-api-core#app-file-explorer)

## Saves, mods e armazenamento interno

| Caminho no armazenamento selecionado | O que fica nele |
| --- | --- |
| `original/` | Disco extraído ou ISO da sua cópia. |
| `game/` | Instalação gerada pelo assistente, quando usada. |
| `save/` | Saves e progresso do jogo. |
| `mods/` | Mods e perfis. Para importar um perfil do SA Mod Manager, inclua `.modloader/profiles/`. |
| `cache/` | Cache de renderização. |

O aplicativo, o `config.toml`, o seletor `storage.txt` e o log `startup.log` ficam no console. Para usar o armazenamento interno, envie um `storage.txt` com o conteúdo `local` e coloque a mídia em `LocalState/original/`. O app usa uma instalação existente em `game/` antes de tentar iniciar pelo disco extraído.

Para migrar com o aplicativo fechado, copie `original/`, `game/`, `save/` e `mods/` do `LocalState` para o `shadow05/` do HD, conforme as pastas que existirem. Ajuste o seletor, abra o jogo e confirme se seu save aparece. Para abrir o assistente manualmente, crie o arquivo vazio `install.txt` em `LocalState` e abra o app.

## Atualizar e solucionar problemas

Para atualizar, feche o jogo e instale o APPX novo sobre a versão existente pelo Xbox Device Portal. Use o mesmo HD com a estrutura `shadow05`. A atualização usa o mesmo identificador de pacote e certificado da versão anterior de teste.

- **O app abriu o instalador.** Confira se `default.xbe` está junto dos demais arquivos extraídos em `shadow05/original/` e veja se o log contém `Media: verified extracted disc. Launching in place.`.
- **O HD não foi selecionado.** Confira as permissões, o conteúdo de `storage-id.txt` e o seletor `storage.txt` no console. Cada unidade preparada deve ter um identificador distinto.
- **Tela preta ou retorno à Dashboard.** Baixe `startup.log` em **File Explorer → LocalAppData → Shadow Recompiled → LocalState** antes de abrir o jogo de novo. Ao relatar o problema, informe a versão, o modelo do Xbox, a etapa em que ocorreu e anexe o log. Compartilhe o log sem arquivos da mídia original nem dados pessoais.

## Compilar

O [guia de compilação UWP](docs/BUILDING-UWP.md#compilar-e-empacotar) lista o Windows SDK, o LLVM e o comando para gerar e assinar o pacote. A compilação usa o `default.xbe` da sua própria cópia.

## Créditos e licenças

O código do port está sob a [GNU GPL v3](LICENSE). A adaptação parte do [ShadowRecomp de FoxWorkshop](https://github.com/FoxWorkshop/ShadowRecomp) e usa [UnleashedRecomp-uwp](https://github.com/YTReviveMe/UnleashedRecomp-uwp) como referência para a integração com o Xbox. A [documentação original](https://github.com/FoxWorkshop/ShadowRecomp/blob/d2be1d4105a83853a3bf1954d3eac2047912d87e/README.md) cobre as versões para Windows e Linux e os créditos do projeto de base.

Consulte os [avisos de terceiros](docs/THIRD-PARTY-UWP.md) para as dependências do pacote. Shadow the Hedgehog é da SEGA.
