> Historical D1 instructions. This package now contains **R1** and retains the capture tools under Account with an Interface R1 marker. See [R1](footer-safe-area-R1.md).

# Diagnóstico D1 — espaço vazio no AGM

A 0.1.32 não resolveu o problema no dispositivo. Este pacote acrescenta medição
e cópia de valores; não volta a modificar os limites da página e não representa
uma nova correção confirmada.

## Recolher o diagnóstico

1. Extrair este pacote e compilar numa **pasta de build nova**, para eliminar a
   possibilidade de recursos QML antigos. A identidade Android continua `pt.lem.app`;
   não é necessário desinstalar a app ou apagar os dados.
2. Confirmar que aparece **Diagnóstico D1** na área Conta. A versão de comunicação
   continua 0.1.32, para funcionar com a política atual do servidor.
3. Na página das perguntas ou dos IDs, reproduzir o espaço vazio com os botões
   escondidos. Sem fazer mais deslocações, tocar no ícone da conta no cabeçalho.
4. Tocar **Diagnóstico da página → Copiar diagnóstico** e colar o texto na conversa.
   A captura é feita antes de abrir Conta, para manter o estado dos menus e a posição
   da página que apresentava o problema.
5. Se for possível, repetir com os botões visíveis e enviar também esse texto.
   Se o problema deixar de acontecer neste build, comunicar esse resultado.

Não é preciso alterar o servidor nem a sua política de versões. O nome Android é
0.1.32-diagnostic1, o código de instalação é 133 e o protocolo continua 0.1.32.
O próximo código de instalação Android deve ser superior a 133.

## O que é recolhido

Revisão da interface, versão Qt real, sistema, dimensões do ecrã/janela/viewport,
visibilidade do teclado, estado de deslocação e dos menus, dimensões da barra,
extensão do conteúdo, posição do último cartão e espaço abaixo dele. Para perguntas,
compara o fim calculado pelo painel com o fim obtido por mapeamento real de coordenadas.
Inclui apenas o nome técnico do item de pergunta (por exemplo PC03.06) e contagens.

Não inclui identificação do carregador/report, respostas, notas, fotografias,
email, nome, endereço do servidor, tokens ou passwords. Não envia o diagnóstico
automaticamente: o botão copia o texto para o utilizador o partilhar.

## Verificar recursos QML

O C++ expõe `qVersion()` e confirma que a raiz QML tem a revisão D1. Se encontrar
uma interface antiga, interrompe o arranque com uma mensagem no log a indicar que
é necessário reconfigurar/compilar num diretório novo. Isto distingue a versão C++
mostrada em Conta da revisão dos recursos QML efetivamente carregados.

## Teste

`python app/tests/test_page_diagnostic.py`: captura antes do modal, barra escondida,
fim mapeado do último cartão, preservação de respostas/posição, exclusão de dados
pessoais, cópia exata para clipboard, catálogo vazio e Gestão. Os testes de reports
e de gestos são executados para verificar que a instrumentação não alterou o fluxo.
Validação QML offscreen/Qt 6.10.2; o caso real continua dependente dos dados do AGM.
