# Nome Lem — 0.1.20

A marca do cliente passa a **Lem**: nome Android, nome e título da aplicação, topo da interface, informação de versão, executável, projeto/alvos CMake, módulo QML, integração desktop e mensagens do código do cliente. O executável é agora `Lem` (`Lem.exe` em Windows). O código Java próprio passa para `pt.lem.app`, com as referências JNI e do manifesto atualizadas.

O logótipo, o ecrã inicial de dois segundos e as funcionalidades mantêm-se. O servidor, deployment, base de dados e respetivos endereços não são renomeados.

## Compatibilidade

A versão atual 0.1.23 usa `pt.lem.app` como identificador Android. É uma nova
identidade de instalação: sincronizar o trabalho pendente antes da troca, pois os
dados privados de outra aplicação não são transferidos automaticamente.

No desktop, as chaves de migração de organização/aplicação continuam a permitir
localizar perfis existentes. As cópias de cache, fotografias e diários mantêm a
localização Lem. O controlador C++ e os ficheiros internos usam agora Lem.

## Compilar e atualizar

No Qt Creator, executar novamente o CMake e selecionar o alvo/executável **Lem**. É recomendável uma pasta de build nova para descartar configurações do antigo módulo QML. O módulo é gerado em `build/qml/Lem`, separado do executável `build/Lem`.

O novo identificador Android exige instalação separada. A assinatura continua necessária para um APK de release. Não é necessário reinicializar a base de dados.

## Verificação

Testes QML em desktop e dimensões Android, verificando o título Lem e as regressões existentes. Testes C++ de migração, incluindo alteração da organização, configuração do servidor e caminhos de fotografias em diários. Integração HTTP local de respostas, fotografias, sincronização, limpeza e remoção. Referências CMake/QML/Java/JNI/desktop verificadas em conjunto; servidor e scripts de deployment comparados com o pacote anterior para garantir que não foram alterados. Não foi executada uma compilação Android ou Windows nesta revisão.
