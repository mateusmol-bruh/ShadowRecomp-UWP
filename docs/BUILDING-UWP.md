# Shadow Recompiled no Xbox Series X|S

Esta adaptação adiciona um alvo UWP x64 para o Modo de Desenvolvedor do Xbox Series X|S. O patch parte do ShadowRecomp em `d2be1d4105a83853a3bf1954d3eac2047912d87e`. Usa o port UWP do Unleashed em `72aefc281a2a0bb0c03bf9bb116ae31dbeee5469` como referência para CoreWindow, Direct3D 12 e a organização dos dados fora do pacote do aplicativo.

O app pode abrir diretamente um disco de Xbox extraído em `original/`. Quando já existe uma instalação em `game/`, ela tem prioridade. O instalador continua disponível para arquivos ISO e para uma instalação solicitada pelo usuário. O port usa o carregador de mods do ShadowRecomp com o formato de perfil do SA Mod Manager.

A entrega contém o APPX UWP x64 1.0.10.0 `ShadowRecomp.UWP.x64.appx`, o certificado público da assinatura de teste `ShadowRecomp-UWP.cer`, o patch e o framework UWP x64 da Microsoft. O framework foi distribuído sem alterações em `Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx`. [Redistribuíveis do Visual Studio](https://learn.microsoft.com/visualstudio/releases/vs18/redistribution)

## Aplicar o patch

No PowerShell, clone a base e aplique o patch fornecido nesta entrega.

```powershell
git clone https://github.com/FoxWorkshop/ShadowRecomp.git
Set-Location ShadowRecomp
git checkout --detach d2be1d4105a83853a3bf1954d3eac2047912d87e
git apply --check --whitespace=nowarn "C:\caminho\ShadowRecomp-UWP.patch"
git apply --whitespace=nowarn "C:\caminho\ShadowRecomp-UWP.patch"
```

## Compilar e empacotar

1. Instale o Visual Studio com desenvolvimento C++ para Desktop e para UWP, LLVM para Windows (`clang-cl.exe` e `lld-link.exe`), CMake, Ninja e Windows SDK 10.0.26100.0.
2. Extraia o `default.xbe` da sua cópia da versão de Xbox original e coloque-o em `ShadowRecompLib\private\default.xbe`. O XboxRecomp2 usa esse arquivo para gerar o C++ durante a configuração. Se já instalou o Shadow Recompiled no PC, procure o arquivo em `game/default.xbe` na pasta do executável. [Instruções de compilação do ShadowRecomp](https://github.com/FoxWorkshop/ShadowRecomp/blob/d2be1d4105a83853a3bf1954d3eac2047912d87e/docs/BUILDING.md)
3. Abra um prompt de ferramentas nativas **x64** para o SDK indicado, inicie o PowerShell nele e execute o script a partir da raiz do repositório. O script baixa os submódulos, compila o SDL3 UWP com MSVC, prepara o conversor de recursos para PC e compila o executável UWP com clang-cl.
4. Forneça um PFX de assinatura cujo certificado seja aceito pelo dispositivo.

```powershell
$password = Read-Host "Senha do PFX" -AsSecureString
.\toolchains\uwp\build.ps1 -CertificatePath "C:\caminho\assinatura.pfx" -CertificatePassword $password
```

O pacote é escrito em `out\uwp\package\ShadowRecomp.UWP.x64.appx`. Sem um PFX, o script gera um pacote sem assinatura. Para atualizar uma instalação existente, mantenha o mesmo assunto do certificado publicador e incremente `-PackageVersion`.

## Instalar o aplicativo no Xbox

1. No Xbox em Modo de Desenvolvedor, abra o **Dev Home**, habilite o **Xbox Device Portal** em **Remote Access Settings** e abra o endereço exibido no navegador do PC. [Configuração do Device Portal](https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/device-portal-xbox)
2. Na página **Home** do portal, use **Add** em **My games & apps** e selecione `ShadowRecomp.UWP.x64.appx`. O pacote usa uma assinatura de teste, e o certificado público correspondente acompanha a entrega. [Implantação manual no Xbox](https://learn.microsoft.com/en-us/windows/uwp/apps-for-xbox/development-options)
3. Ao adicionar as dependências, selecione `Dependencies/x64/Microsoft.VCLibs.x64.14.00.appx`. As DLLs do SDL3 e do DXC já estão no APPX principal.
4. Conecte a unidade preparada antes de abrir o Shadow Recompiled. O app tenta abrir a instalação existente ou a mídia extraída em `shadow05/original`. Se a fonte disponível for uma ISO, siga o assistente.

Para atualizar, feche o jogo e instale o APPX 1.0.10.0 sobre a versão anterior. O pacote usa o mesmo certificado de teste da versão 1.0.9.0. Se o portal rejeitar a atualização, anote o código de erro e faça uma cópia dos arquivos de `LocalState` antes de considerar remover a versão anterior.

## Armazenamento interno ou USB

O app fica instalado no console. A seleção do armazenamento vale para a mídia original, os dados instalados, os saves, os mods e o cache. O `LocalState` fica acessível no explorador de arquivos do Xbox Device Portal. A partir da versão 1.0.10.0, o app procura `shadow05/storage-id.txt` a cada abertura. Ao encontrar um único marcador válido, usa a unidade USB e grava `usb:<identificador>` em `LocalState/storage.txt`. Se não encontrar nenhum, usa os dados internos e grava `local`, mesmo que tenha usado um USB na abertura anterior.

| Pasta | Conteúdo |
| --- | --- |
| `original/` | ISO do Xbox original ou disco extraído. Coloque `default.xbe` na raiz de `original/` ou em uma subpasta imediata, com os demais arquivos extraídos. |
| `game/` | Dados copiados pelo instalador, se ele tiver sido usado. Com o USB ativo, a cópia instalada fica na unidade. |
| `save/` | Saves e progresso, incluindo o redirecionamento configurado no perfil de mods. |
| `mods/` | Mods e perfis do SA Mod Manager. Copie a pasta completa, incluindo `.modloader/profiles/`. |
| `cache/` | Cache de renderização. |

### Ativar USB

1. No PC, crie a pasta `shadow05` na raiz de uma unidade USB NTFS acessível a apps UWP. Conceda **Modificar** a `ALL APPLICATION PACKAGES` nessa pasta e nos seus arquivos e subpastas. Se o Xbox perguntar como usar a unidade, selecione **Mídia**. [Preparação de USB no Modo de Desenvolvedor](https://github.com/XboxEmulationHub/devmodewiki/blob/master/docs/xbox-setup/xbox-developer-mode-setup.mdx)
2. Dentro de `shadow05`, crie `storage-id.txt` com um identificador, por exemplo `shadow-usb`. Use até 64 letras ASCII, números, hífens ou sublinhados. Conecte a unidade ao Xbox.
3. Coloque a mídia em `shadow05/original/` no USB e abra o app. Para um disco extraído, a versão 1.0.3.0 verifica a presença dos arquivos exigidos e o hash de `default.xbe`, depois lê o jogo nessa pasta. Uma ISO ainda passa pelo assistente e ocupa espaço adicional em `shadow05/game/`.

Para o passo 1, este exemplo usa `X:` como letra da unidade **no PC**. Ajuste a letra antes de executar.

```powershell
New-Item -ItemType Directory -Path 'X:\shadow05' -Force
icacls 'X:\shadow05' /grant '*S-1-15-2-1:(OI)(CI)M' /T
Set-Content -Encoding ascii -LiteralPath 'X:\shadow05\storage-id.txt' -Value 'shadow-usb'
```

Se você já usa a pasta `ShadowRecomp` na raiz do USB, feche o app e renomeie essa pasta para `shadow05`. Mantenha o `storage-id.txt` dentro dela.

Para migrar uma instalação, copie `original/`, `game/`, `save/` e `mods/` do `LocalState` para `shadow05/` no USB com o jogo fechado. Confirme que o jogo abre e que o progresso aparece no USB antes de liberar espaço no console. A migração é manual. O `config.toml` e o seletor `storage.txt` ficam no console.

O app identifica a unidade pelo conteúdo de `storage-id.txt`, mesmo se a letra mudar no console. Se várias unidades estiverem preparadas, o app usa o identificador de `LocalState/storage.txt` quando ele corresponde a uma única unidade. Para resolver uma seleção ambígua, feche o app e deixe conectada apenas a unidade desejada ou informe `usb:<identificador>` no arquivo `storage.txt` antes de reabrir. Para usar o armazenamento interno, feche o app, desconecte as unidades preparadas e reabra. A mídia interna fica em `LocalState/original/`.

Para abrir o instalador no armazenamento selecionado, crie um arquivo vazio `LocalState/install.txt` e reabra o aplicativo. O marcador é removido quando o assistente conclui a instalação.

## Diagnosticar falhas na abertura

Abra a versão 1.0.10.0 com a unidade conectada. Se usou `disable-gamepads.txt` no teste anterior, retire esse marcador de `LocalState` para testar com o controle. Se a tela ficar preta ou o jogo voltar para a dashboard, baixe `startup.log` no **File Explorer → LocalAppData → Shadow Recompiled → LocalState** do Xbox Device Portal. O app grava o arquivo no console mesmo quando usa o USB. Cada nova abertura reinicia o arquivo. [Explorador de arquivos do Device Portal](https://learn.microsoft.com/en-us/windows/uwp/debug-test-perf/device-portal-api-core#app-file-explorer)

| Última etapa registrada | O que investigar |
| --- | --- |
| O log do teste não aparece | Ativação do pacote, dependências ou acesso ao `LocalState`. |
| `Storage` | Unidade, identificador, permissões ou seleção do armazenamento. Para testar o armazenamento interno, feche o app e reabra com as unidades preparadas desconectadas. Sem mídia interna, o assistente pode abrir. |
| `Media` | Busca e validação do disco extraído. `Launching in place` confirma que o app escolheu carregar a mídia diretamente. |
| `Kernel` | Resolução das referências a dados do Xbox original na memória emulada. |
| `Video` | Janela WinRT, dispositivo Direct3D 12, cadeia de apresentação ou primeiro quadro. |
| `Input` | Preparação de eventos SDL, leitura nativa do controle ou abertura do controle pelo SDL. A leitura física começa depois do primeiro quadro apresentado e quando a janela tem foco. |
| `Installer` | Descoberta da mídia, leitura dos arquivos ou preparação do cache de shaders. |
| `Installer: audio unavailable` | A saída de som ou os recursos de áudio ficaram indisponíveis. O assistente continua sem a música. |
| `Game audio` | Inicialização da saída de áudio do jogo instalado. |
| `Memory`, `Game` ou `Preparing shader` | Inicialização do jogo instalado. |
| `Unhandled native exception` | Código da exceção, posição relativa à alocação da instrução e endereço acessado e quadros do executável na pilha, quando disponíveis. |

Se o app voltar à dashboard, guarde o primeiro log e faça um teste com o arquivo `disable-gamepads.txt` em `LocalState`. O marcador desativa o controle físico nesse teste e preserva os eventos de teclado. Depois de salvar o novo log, retire o marcador para voltar à leitura normal do controle.

## Estado da validação

Foram verificados o build do SDL3 UWP e do conversor de recursos, a compilação isolada da entrada UWP e do backend CoreWindow com o Windows SDK, a resolução de fontes em uma árvore de teste e o empacotamento com MakeAppx usando arquivos de teste.

O teste de armazenamento no Windows usou duas letras temporárias para verificar a seleção automática e explícita, a preservação de uma instalação interna existente, a escrita dos saves, a descoberta de ISO e disco extraído e a rejeição de seleções ambíguas.

Os dois testes da versão 1.0.4.0 no Xbox validaram o disco extraído em `E:\shadow05`, carregaram o XBE e inicializaram o Direct3D 12. Ambos registraram `0xC0000005` logo ao iniciar o código recompilado, com e sem a leitura dos controles. Os símbolos e a desmontagem do executável localizam a falha na primeira gravação na pilha de `_xstart`. O contexto inicial deixava `ESP` zerado e a instrução escrevia fora da pilha. A versão 1.0.5.0 inicializa `ESP` com a pilha alocada e usa endereços da memória emulada para os dados locais das threads. O teste no Windows reproduziu a exceção com o contexto anterior e verificou a primeira gravação e a inicialização dos dados locais com a correção. [Violação de acesso de leitura ou gravação](https://learn.microsoft.com/en-us/shows/inside/access-violation-c0000005-read-or-write)

O teste da versão 1.0.5.0 no Xbox chegou ao loop de eventos e depois caiu ao consultar `XboxHardwareInfo`. A referência mantinha o ordinal da importação quando o código esperava o endereço de uma estrutura na memória emulada. A versão 1.0.6.0 resolve as 13 importações de dados usadas pela mídia testada antes de iniciar o jogo. Um teste isolado no Windows usou o XBE original para reproduzir a falha e verificou que a função recompilada passou pela instrução após o novo mapeamento. O log de 1.0.6.0 no Xbox confirmou o carregamento das 13 importações. [Tabela de exportações do kernel do Xbox](https://github.com/XboxDev/xbedump/blob/master/xboxkrnl.h)

No teste de 1.0.6.0, o Xbox encontrou o disco extraído em `E:\shadow05` e inicializou o Direct3D 12. A falha seguinte ocorreu na inicialização do Direct3D do Xbox original, ao zerar um buffer recém-alocado. As rotinas de alocação retornavam endereços emulados como se fossem ponteiros do processo, o que fazia a ponte converter o endereço duas vezes. A versão 1.0.7.0 corrige a passagem dos ponteiros de alocação e liberação e retorna zero se o heap físico não tiver espaço. O `startup.log` agora registra a base da memória emulada para identificar a região acessada em caso de erro. Um teste no Windows com o XBE original, as rotinas de alocação do aplicativo e um backbuffer de teste reproduziu a falha anterior e concluiu a inicialização do Direct3D emulado com a correção. O teste de 1.0.7.0 no Xbox passou dessa instrução e registrou uma nova falha de gravação.

O teste de 1.0.7.0 no Xbox registrou uma gravação em `0x80000040` na memória emulada. O teste de inicialização no Windows reproduziu a mesma falha com o disco original e identificou a rotina que usa a região fixa de `0x80000000` a `0x80000FFF`. A versão 1.0.8.0 disponibiliza essa página de 4 KiB e adiciona os quadros do executável à pilha do `startup.log` quando ocorre uma violação de acesso. A partida de teste no Windows carregou o disco extraído diretamente, apresentou quadros com Direct3D 12 em software e avançou pelas telas de abertura. A execução instrumentada registrou pelo menos 300 quadros da lógica do jogo com uma saída de áudio de teste. A inspeção visual confirmou a tela de título com o aviso de Start. O teste de 1.0.8.0 no Xbox chegou à fase e apresentou uma falha ao abrir Opções e Conquistas pela pausa.

A versão 1.0.9.0 corrige o retorno da rotina do menu de pausa ao abrir Opções ou Conquistas. A rotina mantém a convenção de chamada do jogo original e remove da pilha emulada o argumento da atualização do quadro. O teste com o disco original no Windows reproduziu a queda das duas abas em Westopolis antes da correção. A versão corrigida abriu e fechou as duas abas pela pausa e retomou a fase na mesma partida. O teste no Xbox Series X|S confirmou o funcionamento das duas abas no menu de pausa.

O alvo Xbox seleciona Direct3D 12 e usa o backend WinRT de um fork do SDL3. A versão 1.0.1.0 adia a alocação da memória emulada até a execução do jogo instalado. Ela reserva o espaço de endereçamento de 32 bits e usa 256 MiB para cada heap emulado. O perfil de memória e mods com alto consumo requerem teste no dispositivo. O instalador mantém o processamento de eventos da janela enquanto aguarda a apresentação de quadros. [SDL3 UWP](https://github.com/XboxEmulationHub/SDL3-uwp/tree/ec2e0e7c77ec9a04437eb3ae884d62ae0e6129b4) · [Recursos UWP no Xbox](https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/system-resource-allocation) · [DXGI Present](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)

[ShadowRecomp, base do patch](https://github.com/FoxWorkshop/ShadowRecomp/tree/d2be1d4105a83853a3bf1954d3eac2047912d87e) · [UnleashedRecomp UWP, referência da plataforma](https://github.com/YTReviveMe/UnleashedRecomp-uwp/tree/72aefc281a2a0bb0c03bf9bb116ae31dbeee5469)
